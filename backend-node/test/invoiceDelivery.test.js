import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { EmailService } from '../src/services/emailService.js';
import { InvoiceDeliveryService } from '../src/services/invoiceDeliveryService.js';
import { generateInvoicePdf } from '../src/services/invoicePdfGenerator.js';
import { loadSmtpConfig } from '../src/services/smtpConfig.js';
import { InvoiceRepository } from '../src/repositories/invoiceRepository.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const apiEnvironment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_invoice_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const smtpEnvironment = {
  VRMS_SMTP_HOST: 'smtp.example.test',
  VRMS_SMTP_PORT: '587',
  VRMS_SMTP_USERNAME: 'smtp-test-user',
  VRMS_SMTP_PASSWORD: 'smtp-test-secret',
  VRMS_SMTP_FROM_EMAIL: 'invoices@example.test',
  VRMS_SMTP_FROM_NAME: 'Northstar Mobility',
  VRMS_SMTP_SECURITY: 'starttls',
  VRMS_SMTP_TIMEOUT_SECONDS: '20',
};

const invoiceDocument = {
  invoiceId: 'INV-PDF-001',
  bookingId: 'BK-PDF-001',
  customerId: 'CUST-PDF-001',
  customerName: 'Jamie Customer',
  customerEmail: 'jamie@example.test',
  vehicleId: 'CAR-PDF-001',
  vehicleBrand: 'Toyota',
  vehicleModel: 'Camry',
  vehicleType: 'Car',
  rentalStartDate: '2026-09-01',
  rentalEndDate: '2026-09-06',
  durationDays: 5,
  dailyRate: 1800,
  subtotal: 9000,
  discount: 500,
  tax: 1530,
  total: 10030,
  paymentStatus: 'Successful',
};

const servers = new Set();

async function startApp(database, emailService) {
  const app = createApp({
    config: loadConfig(apiEnvironment),
    database,
    emailService,
  });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return `http://127.0.0.1:${server.address().port}`;
}

async function tokenFor(database, userId, role = 'CUSTOMER') {
  database.seedUser({
    user_id: userId,
    role,
    name: userId,
    email: `${userId}@example.test`,
  });
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

function seedDeliveryRecords(database, {
  invoiceId = 'INV-DELIVERY-001',
  bookingId = 'BK-DELIVERY-001',
  customerId = 'delivery-customer',
  status = 'PENDING',
} = {}) {
  database.seedUser({
    user_id: customerId,
    name: 'Jamie Customer',
    email: 'jamie@example.test',
    role: 'CUSTOMER',
  });
  database.seedVehicle({
    vehicle_id: 'CAR-DELIVERY-001',
    brand: 'Toyota',
    model: 'Camry',
    vehicle_type: 'Car',
    daily_rate: '1800.00',
    is_available: true,
  });
  database.seedBooking({
    booking_id: bookingId,
    customer_id: customerId,
    vehicle_id: 'CAR-DELIVERY-001',
    start_date: '2026-09-01',
    end_date: '2026-09-06',
    status: 'CONFIRMED',
  });
  database.payments.set(`PAY-${bookingId}`, {
    payment_id: `PAY-${bookingId}`,
    booking_id: bookingId,
    amount: '9000.00',
    payment_method: 'UPI',
    payment_status: 'Successful',
  });
  database.invoices.set(invoiceId, {
    invoice_id: invoiceId,
    booking_id: bookingId,
    subtotal: '9000.00',
    discount: '0.00',
    tax: '0.00',
    total_amount: '9000.00',
    email_status: status,
    email_attempts: status === 'FAILED' ? 1 : 0,
    email_last_attempt_at: null,
    email_sent_at: null,
    email_last_error: status === 'FAILED' ? 'previous safe failure' : null,
  });
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(() => resolve());
  })));
  servers.clear();
});

describe('invoice PDF generation', () => {
  it('creates a PDF containing customer, booking, vehicle, rental, amount and payment details', async () => {
    const pdf = await generateInvoicePdf(invoiceDocument);
    const content = [...pdf.toString('latin1').matchAll(/<([0-9a-fA-F]+)>/g)]
      .map((match) => Buffer.from(match[1], 'hex').toString('latin1'))
      .join('');

    assert.equal(pdf.subarray(0, 5).toString('ascii'), '%PDF-');
    for (const required of [
      'NORTHSTAR MOBILITY', 'INV-PDF-001', 'Jamie Customer', 'jamie@example.test',
      'CUST-PDF-001', 'BK-PDF-001', 'Toyota Camry (Car)', 'CAR-PDF-001',
      '2026-09-01 to 2026-09-06', '5 days x INR 1800.00 / day',
      'INR 9000.00', 'INR 500.00', 'INR 1530.00', 'INR 10030.00', 'Successful',
      'simulated payments; no real charge was made',
    ]) {
      assert.ok(content.includes(required), `PDF is missing ${required}`);
    }
  });
});

describe('SMTP invoice email service', () => {
  it('uses environment configuration, STARTTLS and a PDF attachment', async () => {
    let transportOptions;
    let mail;
    const email = new EmailService({
      environment: smtpEnvironment,
      transportFactory(options) {
        transportOptions = options;
        return {
          async sendMail(message) { mail = message; },
          close() {},
        };
      },
    });
    const pdf = Buffer.from('%PDF-test-invoice');

    await email.sendInvoice('customer@example.test', 'INV-SMTP-001', pdf);

    assert.equal(transportOptions.host, smtpEnvironment.VRMS_SMTP_HOST);
    assert.equal(transportOptions.port, 587);
    assert.equal(transportOptions.secure, false);
    assert.equal(transportOptions.requireTLS, true);
    assert.equal(transportOptions.auth.user, smtpEnvironment.VRMS_SMTP_USERNAME);
    assert.equal(transportOptions.auth.pass, smtpEnvironment.VRMS_SMTP_PASSWORD);
    assert.equal(transportOptions.logger, false);
    assert.equal(transportOptions.debug, false);
    assert.deepEqual(mail.from, {
      name: 'Northstar Mobility',
      address: 'invoices@example.test',
    });
    assert.equal(mail.to, 'customer@example.test');
    assert.equal(mail.attachments.length, 1);
    assert.equal(mail.attachments[0].filename, 'invoice-INV-SMTP-001.pdf');
    assert.equal(mail.attachments[0].contentType, 'application/pdf');
    assert.deepEqual(mail.attachments[0].content, pdf);
  });

  it('sanitizes SMTP failures and reports missing configuration without echoing secrets', async () => {
    const secret = 'smtp-password-must-not-appear';
    const email = new EmailService({
      environment: { ...smtpEnvironment, VRMS_SMTP_PASSWORD: secret },
      transportFactory: () => ({
        async sendMail() { throw new Error(`authentication failed: ${secret}`); },
      }),
    });
    await assert.rejects(
      email.sendInvoice('customer@example.test', 'INV-SMTP-ERR', Buffer.from('%PDF-test')),
      (error) => {
        assert.equal(error.message, 'SMTP server did not accept the invoice message');
        assert.equal(error.message.includes(secret), false);
        return true;
      },
    );

    const factoryFailure = new EmailService({
      environment: smtpEnvironment,
      transportFactory() { throw new Error(`private configuration ${secret}`); },
    });
    await assert.rejects(
      factoryFailure.sendInvoice('customer@example.test', 'INV-SMTP-FACTORY', Buffer.from('%PDF-test')),
      (error) => {
        assert.equal(error.message, 'SMTP server did not accept the invoice message');
        assert.equal(error.message.includes(secret), false);
        return true;
      },
    );

    assert.throws(() => loadSmtpConfig({ VRMS_SMTP_PASSWORD: secret }), (error) => {
      assert.match(error.message, /VRMS_SMTP_HOST/);
      assert.equal(error.message.includes(secret), false);
      return true;
    });
  });
});

describe('invoice email delivery state', () => {
  it('claims PENDING atomically, marks SENT, and prevents another send', async () => {
    const database = new FakePostgres();
    seedDeliveryRecords(database);
    let sends = 0;
    const delivery = new InvoiceDeliveryService({
      repository: new InvoiceRepository(database),
      emailService: { async sendInvoice(recipient, invoiceId, pdf) {
        sends += 1;
        assert.equal(recipient, 'jamie@example.test');
        assert.equal(invoiceId, 'INV-DELIVERY-001');
        assert.equal(pdf.subarray(0, 5).toString('ascii'), '%PDF-');
      } },
    });

    const sent = await delivery.sendInvoice('INV-DELIVERY-001');
    const duplicate = await delivery.sendInvoice('INV-DELIVERY-001');

    assert.equal(sent.status, 'SENT');
    assert.equal(sent.sent, true);
    assert.equal(duplicate.status, 'SENT');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_attempts, 1);
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_sent_at !== null, true);
    assert.equal(sends, 1);
  });

  it('records FAILED safely and allows a later retry to reach SENT', async () => {
    const database = new FakePostgres();
    seedDeliveryRecords(database);
    let shouldFail = true;
    const delivery = new InvoiceDeliveryService({
      repository: new InvoiceRepository(database),
      emailService: { async sendInvoice() {
        if (shouldFail) throw new Error('private SMTP response with credential data');
      } },
    });

    const failed = await delivery.sendInvoice('INV-DELIVERY-001');
    assert.equal(failed.status, 'FAILED');
    assert.match(failed.message, /administrator can retry/);
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_attempts, 1);
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_last_error,
      'SMTP delivery failed; check server logs and configuration');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_last_error.includes('credential'), false);

    shouldFail = false;
    const retried = await delivery.sendInvoice('INV-DELIVERY-001');
    assert.equal(retried.status, 'SENT');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_attempts, 2);
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_last_error, null);
  });

  it('allows only one concurrent delivery claim and avoids duplicate sends', async () => {
    const database = new FakePostgres();
    seedDeliveryRecords(database);
    let sendCount = 0;
    let enteredSend;
    const sending = new Promise((resolve) => { enteredSend = resolve; });
    let finishSend;
    const heldSend = new Promise((resolve) => { finishSend = resolve; });
    const delivery = new InvoiceDeliveryService({
      repository: new InvoiceRepository(database),
      pdfGenerator: async () => Buffer.from('%PDF-concurrent'),
      emailService: { async sendInvoice() {
        sendCount += 1;
        enteredSend();
        await heldSend;
      } },
    });

    const firstPromise = delivery.sendInvoice('INV-DELIVERY-001');
    await sending;
    const concurrent = await delivery.sendInvoice('INV-DELIVERY-001');
    assert.equal(concurrent.status, 'SENDING');
    assert.equal(sendCount, 1);
    finishSend();
    const first = await firstPromise;
    assert.equal(first.status, 'SENT');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_attempts, 1);
    assert.equal(sendCount, 1);
  });

  it('keeps SENDING if SMTP accepted the message but SENT persistence fails', async () => {
    const database = new FakePostgres();
    seedDeliveryRecords(database);
    database.failInvoiceStatusUpdate = true;
    let sends = 0;
    const delivery = new InvoiceDeliveryService({
      repository: new InvoiceRepository(database),
      pdfGenerator: async () => Buffer.from('%PDF-status-write'),
      emailService: { async sendInvoice() { sends += 1; } },
    });

    const first = await delivery.sendInvoice('INV-DELIVERY-001');
    database.failInvoiceStatusUpdate = false;
    const retry = await delivery.sendInvoice('INV-DELIVERY-001');

    assert.equal(first.status, 'SENDING');
    assert.match(first.message, /Do not resend/);
    assert.equal(retry.status, 'SENDING');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_status, 'SENDING');
    assert.equal(database.invoices.get('INV-DELIVERY-001').email_attempts, 1);
    assert.equal(sends, 1);
  });
});

describe('payment email integration and admin retry', () => {
  it('keeps a successful payment and invoice when SMTP fails', async () => {
    const database = new FakePostgres();
    const customerToken = await tokenFor(database, 'mail-failure-customer');
    database.seedVehicle({
      vehicle_id: 'CAR-MAIL-FAIL', brand: 'Honda', model: 'City',
      vehicle_type: 'Car', daily_rate: '1000.00', is_available: true,
    });
    database.seedBooking({
      booking_id: 'BK-MAIL-FAIL', customer_id: 'mail-failure-customer',
      vehicle_id: 'CAR-MAIL-FAIL', start_date: '2026-10-01', end_date: '2026-10-03',
      status: 'CONFIRMED',
    });
    const baseUrl = await startApp(database, {
      async sendInvoice() { throw new Error('private SMTP credential detail'); },
    });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token: customerToken,
      body: { bookingId: 'BK-MAIL-FAIL', method: 'UPI' },
    });

    assert.equal(result.response.status, 201);
    assert.equal(result.body.status, 'Successful');
    assert.equal(result.body.emailStatus, 'FAILED');
    assert.match(result.body.emailMessage, /administrator can retry/);
    assert.equal(database.payments.size, 1);
    assert.equal(database.invoices.size, 1);
    assert.equal(database.invoices.get(result.body.invoiceId).email_status, 'FAILED');
    assert.equal(database.invoices.get(result.body.invoiceId).email_last_error.includes('credential'), false);
  });

  it('restricts retry to admins and preserves the retry response shape', async () => {
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
        if (shouldFail) throw new Error('private SMTP credentials');
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
