import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import PDFDocument from 'pdfkit';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { InvoiceRepository } from '../src/repositories/invoiceRepository.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { EmailService } from '../src/services/emailService.js';
import { InvoiceDeliveryService } from '../src/services/invoiceDeliveryService.js';
import { generateInvoicePdf } from '../src/services/invoicePdfGenerator.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const apiEnvironment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_invoice_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const invoiceDocument = {
  invoiceId: 'INV-TEST-100',
  bookingId: 'BOOK-TEST-100',
  customerId: 'CUST-TEST-100',
  customerName: 'Jordan Taylor',
  customerEmail: 'jordan@example.test',
  vehicleId: 'CAR-TEST-100',
  vehicleBrand: 'Toyota',
  vehicleModel: 'Camry',
  vehicleType: 'Car',
  rentalStartDate: '2026-12-02',
  rentalEndDate: '2026-12-05',
  durationDays: 4,
  dailyRate: 1000,
  subtotal: 4000,
  discount: 100,
  tax: 390,
  total: 4290,
  paymentStatus: 'Successful',
};

const fakeResendEnvironment = {
  RESEND_API_KEY: 're_test_fake_key_1234567890',
  RESEND_FROM_EMAIL: 'billing@northstar.example',
  RESEND_FROM_NAME: 'Northstar Mobility',
};

const samplePdf = Buffer.from('%PDF-1.4\nmock invoice bytes\n%%EOF', 'ascii');

class FakeInvoiceRepository {
  constructor({ emailStatus = 'PENDING', invoice = invoiceDocument } = {}) {
    this.emailStatus = emailStatus;
    this.invoice = invoice;
    this.emailAttempts = 0;
    this.claims = 0;
  }

  async claimInvoiceEmail(invoiceId) {
    if (invoiceId !== this.invoice.invoiceId ||
        !['PENDING', 'FAILED'].includes(this.emailStatus)) return false;
    this.claims += 1;
    this.emailAttempts += 1;
    this.emailStatus = 'SENDING';
    return true;
  }

  async getInvoiceEmailStatus(invoiceId) {
    return invoiceId === this.invoice.invoiceId ? this.emailStatus : null;
  }

  async loadInvoiceDocument(invoiceId) {
    return invoiceId === this.invoice.invoiceId ? this.invoice : null;
  }

  async markInvoiceEmailSent(invoiceId) {
    if (invoiceId !== this.invoice.invoiceId || this.emailStatus !== 'SENDING') {
      throw new Error('Unexpected email state');
    }
    if (this.failSentUpdate) throw new Error('simulated state persistence failure');
    this.emailStatus = 'SENT';
  }

  async markInvoiceEmailFailed(invoiceId) {
    if (invoiceId !== this.invoice.invoiceId || this.emailStatus !== 'SENDING') {
      throw new Error('Unexpected email state');
    }
    this.emailStatus = 'FAILED';
  }
}

const servers = new Set();

async function startApp(database, emailService) {
  const app = createApp({ config: loadConfig(apiEnvironment), database, emailService });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return `http://127.0.0.1:${server.address().port}`;
}

async function tokenFor(database, userId, role = 'CUSTOMER') {
  database.seedUser({ user_id: userId, role, name: userId, email: `${userId}@example.test` });
  return new SessionRepository(database).createSession(userId, role);
}

async function request(baseUrl, path, { method = 'GET', body, token } = {}) {
  const response = await fetch(`${baseUrl}${path}`, {
    method,
    headers: {
      ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  return { response, body: await response.json() };
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(resolve);
  })));
  servers.clear();
});

describe('invoice PDF generation', () => {
  it('creates a PDF with customer, booking, vehicle, rental, amount, and payment details', async () => {
    const recordedText = [];
    const originalText = PDFDocument.prototype.text;
    PDFDocument.prototype.text = function recordPdfText(value, ...args) {
      if (typeof value === 'string') recordedText.push(value);
      return originalText.call(this, value, ...args);
    };
    let pdf;
    try {
      pdf = await generateInvoicePdf(invoiceDocument);
    } finally {
      PDFDocument.prototype.text = originalText;
    }
    const content = recordedText.join('\n');

    assert.equal(pdf.subarray(0, 5).toString('ascii'), '%PDF-');
    for (const required of [
      'Jordan Taylor', 'jordan@example.test', 'BOOK-TEST-100', 'Toyota Camry',
      'CAR-TEST-100', '2026-12-02 to 2026-12-05', '4 days',
      'INR 1000.00 / day', 'INR 4000.00', 'INR 100.00', 'INR 390.00',
      'INR 4290.00', 'Successful',
    ]) {
      assert.ok(content.includes(required), `PDF is missing ${required}`);
    }
  });
});

describe('Resend invoice email service', () => {
  it('sends a PDF attachment through the injected Resend SDK client', async () => {
    let receivedKey;
    let sentOptions;
    const service = new EmailService({
      environment: fakeResendEnvironment,
      resendFactory(apiKey) {
        receivedKey = apiKey;
        return {
          emails: {
            async send(options) {
              sentOptions = options;
              return { data: { id: 'email_test_123' }, error: null };
            },
          },
        };
      },
    });

    await service.sendInvoice('jordan@example.test', 'INV-TEST-100', samplePdf);

    assert.equal(receivedKey, fakeResendEnvironment.RESEND_API_KEY);
    assert.deepEqual(sentOptions, {
      from: 'Northstar Mobility <billing@northstar.example>',
      to: ['jordan@example.test'],
      subject: 'Your vehicle rental invoice INV-TEST-100',
      text: 'Hello,\n\nYour vehicle rental invoice INV-TEST-100 is attached as a PDF.\n\n'
        + 'This application uses simulated payments; no real payment was processed.\n',
      attachments: [{
        filename: 'invoice-INV-TEST-100.pdf',
        content: samplePdf,
        contentType: 'application/pdf',
      }],
    });
  });

  it('rejects a missing Resend API key without constructing or calling the SDK', async () => {
    let factoryCalled = false;
    const service = new EmailService({
      environment: { ...fakeResendEnvironment, RESEND_API_KEY: '' },
      resendFactory() {
        factoryCalled = true;
        throw new Error('should not be called');
      },
    });

    await assert.rejects(
      service.sendInvoice('jordan@example.test', 'INV-TEST-100', samplePdf),
      { message: 'Resend API key is not configured' },
    );
    assert.equal(factoryCalled, false);
  });

  it('sanitizes provider-reported errors and thrown SDK errors', async (t) => {
    await t.test('provider error response', async () => {
      const service = new EmailService({
        environment: fakeResendEnvironment,
        resendFactory: () => ({
          emails: {
            async send() {
              return { data: null, error: { message: 'private provider response and secret detail' } };
            },
          },
        }),
      });

      await assert.rejects(
        service.sendInvoice('jordan@example.test', 'INV-TEST-100', samplePdf),
        (error) => {
          assert.equal(error.message, 'Resend server did not accept the invoice message');
          assert.doesNotMatch(error.message, /private provider|secret detail|re_test_fake_key/i);
          return true;
        },
      );
    });

    await t.test('thrown SDK error', async () => {
      const service = new EmailService({
        environment: fakeResendEnvironment,
        resendFactory: () => ({
          emails: {
            async send() {
              throw new Error('request failed with re_test_fake_key_1234567890 and private details');
            },
          },
        }),
      });

      await assert.rejects(
        service.sendInvoice('jordan@example.test', 'INV-TEST-100', samplePdf),
        (error) => {
          assert.equal(error.message, 'Resend server did not accept the invoice message');
          assert.doesNotMatch(error.message, /re_test_fake_key|private details/i);
          return true;
        },
      );
    });
  });

  it('validates recipient, invoice ID, and PDF before calling Resend', async () => {
    let factoryCalls = 0;
    const service = new EmailService({
      environment: fakeResendEnvironment,
      resendFactory() {
        factoryCalls += 1;
        return { emails: { async send() { return { data: { id: 'unused' }, error: null }; } } };
      },
    });

    await assert.rejects(service.sendInvoice('invalid-address', 'INV-1', samplePdf), /recipient email is invalid/);
    await assert.rejects(service.sendInvoice('jordan@example.test', 'INV/1', samplePdf), /Invoice ID is not valid/);
    await assert.rejects(service.sendInvoice('jordan@example.test', 'INV-1', Buffer.from('not a PDF')), /not a valid PDF/);
    assert.equal(factoryCalls, 0);
  });
});

describe('invoice delivery state and retry behavior', () => {
  it('generates and attaches a PDF, then persists SENT', async () => {
    const repository = new FakeInvoiceRepository();
    let sent;
    const service = new InvoiceDeliveryService({
      repository,
      emailService: {
        async sendInvoice(recipient, invoiceId, pdf) {
          sent = { recipient, invoiceId, pdf };
        },
      },
    });

    const result = await service.sendInvoice(invoiceDocument.invoiceId);

    assert.equal(result.status, 'SENT');
    assert.equal(result.sent, true);
    assert.equal(repository.emailStatus, 'SENT');
    assert.equal(repository.emailAttempts, 1);
    assert.equal(sent.recipient, invoiceDocument.customerEmail);
    assert.equal(sent.invoiceId, invoiceDocument.invoiceId);
    assert.equal(sent.pdf.subarray(0, 5).toString('ascii'), '%PDF-');
  });

  it('records FAILED safely and allows a retry to reach SENT', async () => {
    const repository = new FakeInvoiceRepository();
    let sendCount = 0;
    const service = new InvoiceDeliveryService({
      repository,
      pdfGenerator: async () => samplePdf,
      emailService: {
        async sendInvoice() {
          sendCount += 1;
          if (sendCount === 1) throw new Error('private Resend key and provider response');
        },
      },
    });

    const failed = await service.sendInvoice(invoiceDocument.invoiceId);
    assert.equal(failed.status, 'FAILED');
    assert.equal(failed.sent, false);
    assert.equal(repository.emailStatus, 'FAILED');
    assert.equal(repository.emailAttempts, 1);
    assert.doesNotMatch(JSON.stringify(failed), /private Resend key|provider response/i);

    const retried = await service.sendInvoice(invoiceDocument.invoiceId);
    assert.equal(retried.status, 'SENT');
    assert.equal(repository.emailStatus, 'SENT');
    assert.equal(repository.emailAttempts, 2);
    assert.equal(sendCount, 2);
  });

  it('atomically prevents concurrent and duplicate sends', async () => {
    const repository = new FakeInvoiceRepository();
    let sendCount = 0;
    let releaseSend;
    let signalStarted;
    const started = new Promise((resolve) => { signalStarted = resolve; });
    const blockedSend = new Promise((resolve) => { releaseSend = resolve; });
    const service = new InvoiceDeliveryService({
      repository,
      pdfGenerator: async () => samplePdf,
      emailService: {
        async sendInvoice() {
          sendCount += 1;
          signalStarted();
          await blockedSend;
        },
      },
    });

    const firstAttempt = service.sendInvoice(invoiceDocument.invoiceId);
    await started;
    const concurrentAttempt = await service.sendInvoice(invoiceDocument.invoiceId);
    assert.equal(concurrentAttempt.status, 'SENDING');
    assert.equal(concurrentAttempt.sent, false);

    releaseSend();
    assert.equal((await firstAttempt).status, 'SENT');
    const duplicateAttempt = await service.sendInvoice(invoiceDocument.invoiceId);
    assert.equal(duplicateAttempt.status, 'SENT');
    assert.equal(duplicateAttempt.sent, false);
    assert.equal(sendCount, 1);
    assert.equal(repository.emailAttempts, 1);
  });

  it('keeps SENDING when the provider accepted the PDF but SENT persistence fails', async () => {
    const repository = new FakeInvoiceRepository();
    repository.failSentUpdate = true;
    let sendCount = 0;
    const service = new InvoiceDeliveryService({
      repository,
      pdfGenerator: async () => samplePdf,
      emailService: { async sendInvoice() { sendCount += 1; } },
    });

    const first = await service.sendInvoice(invoiceDocument.invoiceId);
    repository.failSentUpdate = false;
    const retry = await service.sendInvoice(invoiceDocument.invoiceId);

    assert.equal(first.status, 'SENDING');
    assert.match(first.message, /Do not resend/);
    assert.equal(retry.status, 'SENDING');
    assert.equal(repository.emailStatus, 'SENDING');
    assert.equal(repository.emailAttempts, 1);
    assert.equal(sendCount, 1);
  });
});

describe('payment email integration and admin retry', () => {
  it('keeps a successful payment and invoice when the email provider fails', async () => {
    const database = new FakePostgres();
    const customerToken = await tokenFor(database, 'email-failure-customer');
    database.seedVehicle({
      vehicle_id: 'CAR-EMAIL-FAIL', brand: 'Honda', model: 'City',
      vehicle_type: 'Car', daily_rate: '1000.00', is_available: true,
    });
    database.seedBooking({
      booking_id: 'BK-EMAIL-FAIL', customer_id: 'email-failure-customer',
      vehicle_id: 'CAR-EMAIL-FAIL', start_date: '2026-10-01', end_date: '2026-10-03',
      status: 'CONFIRMED',
    });
    const baseUrl = await startApp(database, {
      async sendInvoice() { throw new Error('private Resend credential detail'); },
    });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token: customerToken,
      body: { bookingId: 'BK-EMAIL-FAIL', method: 'UPI' },
    });

    assert.equal(result.response.status, 201);
    assert.equal(result.body.status, 'Successful');
    assert.equal(result.body.emailStatus, 'FAILED');
    assert.match(result.body.emailMessage, /administrator can retry/);
    assert.equal(database.payments.size, 1);
    assert.equal(database.invoices.size, 1);
    assert.equal(database.invoices.get(result.body.invoiceId).email_status, 'FAILED');
    assert.doesNotMatch(database.invoices.get(result.body.invoiceId).email_last_error, /credential|Resend/i);
  });

  it('restricts invoice retry to admins and preserves the retry response', async () => {
    const database = new FakePostgres();
    const customerToken = await tokenFor(database, 'retry-owner-customer');
    const adminToken = await tokenFor(database, 'retry-admin', 'ADMIN');
    database.seedVehicle({
      vehicle_id: 'CAR-RETRY', brand: 'Honda', model: 'City',
      vehicle_type: 'Car', daily_rate: '1000.00', is_available: true,
    });
    database.seedBooking({
      booking_id: 'BK-RETRY', customer_id: 'retry-owner-customer',
      vehicle_id: 'CAR-RETRY', start_date: '2026-10-01', end_date: '2026-10-03',
      status: 'CONFIRMED',
    });
    let shouldFail = true;
    const baseUrl = await startApp(database, {
      async sendInvoice() {
        if (shouldFail) throw new Error('private Resend credentials');
      },
    });
    const first = await request(baseUrl, '/api/payments', {
      method: 'POST', token: customerToken,
      body: { bookingId: 'BK-RETRY', method: 'Card' },
    });
    assert.equal(first.body.emailStatus, 'FAILED');

    const denied = await request(baseUrl, `/api/admin/invoices/${first.body.invoiceId}/retry-email`, {
      method: 'POST', token: customerToken,
    });
    assert.equal(denied.response.status, 403);

    shouldFail = false;
    const retried = await request(baseUrl, `/api/admin/invoices/${first.body.invoiceId}/retry-email`, {
      method: 'POST', token: adminToken,
    });
    assert.equal(retried.response.status, 200);
    assert.deepEqual(Object.keys(retried.body), ['invoiceId', 'emailStatus', 'message']);
    assert.equal(retried.body.invoiceId, first.body.invoiceId);
    assert.equal(retried.body.emailStatus, 'SENT');
    assert.equal(database.invoices.get(first.body.invoiceId).email_attempts, 2);
    assert.equal(database.payments.size, 1);
  });
});
