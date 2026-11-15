import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const environment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_payments_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const servers = new Set();
const successfulMockEmail = { async sendInvoice() {} };

async function startApp(database = new FakePostgres(), overrides = {}, emailService = successfulMockEmail) {
  const config = loadConfig({ ...environment, ...overrides });
  const app = createApp({ config, database, emailService });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return { baseUrl: `http://127.0.0.1:${server.address().port}`, database };
}

async function tokenFor(database, userId, role = 'CUSTOMER') {
  database.seedUser({
    user_id: userId,
    role,
    name: userId,
    email: `${userId}@example.com`,
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

function seedVehicle(database, vehicleId = 'PAY-CAR') {
  database.seedVehicle({
    vehicle_id: vehicleId,
    brand: 'Honda',
    model: 'City',
    vehicle_type: 'Car',
    daily_rate: '1200.00',
    is_available: true,
  });
}

function seedBooking(database, {
  bookingId = 'BK-PAYMENT',
  customerId = 'payment-customer',
  vehicleId = 'PAY-CAR',
  startDate = '2026-08-01',
  endDate = '2026-08-04',
  status = 'CONFIRMED',
} = {}) {
  database.seedBooking({
    booking_id: bookingId,
    customer_id: customerId,
    vehicle_id: vehicleId,
    start_date: startDate,
    end_date: endDate,
    status,
  });
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(() => resolve());
  })));
  servers.clear();
});

describe('payment and invoice API', () => {
  it('supports Card, UPI and Cash as simulated payments without accepting or storing card data', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'payment-method-customer');
    seedVehicle(database);

    for (const [index, method] of ['Card', 'UPI', 'Cash'].entries()) {
      const bookingId = `BK-METHOD-${index}`;
      seedBooking(database, { bookingId, customerId: 'payment-method-customer' });
      const result = await request(baseUrl, '/api/payments', {
        method: 'POST', token,
        body: {
          bookingId,
          method,
          cardNumber: 'must-be-ignored',
          cvv: 'must-be-ignored',
        },
      });

      assert.equal(result.response.status, 201);
      assert.equal(result.body.status, 'Successful');
      assert.equal(result.body.simulation, true);
      assert.equal(database.payments.get(result.body.paymentId).payment_method, method);
      assert.equal(JSON.stringify([...database.payments.values()]).includes('must-be-ignored'), false);
    }
    assert.equal(database.payments.size, 3);
    assert.equal(database.invoices.size, 3);
  });

  it('rejects unsupported methods without creating a payment or invoice', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'invalid-method-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'invalid-method-customer' });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'Bitcoin' },
    });

    assert.equal(result.response.status, 400);
    assert.deepEqual(result.body, { error: 'method must be Card, UPI or Cash' });
    assert.equal(database.payments.size, 0);
    assert.equal(database.invoices.size, 0);
  });

  it('does not allow a customer to pay another customer’s booking', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'wrong-payment-owner');
    seedVehicle(database);
    seedBooking(database, { customerId: 'actual-booking-owner' });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'UPI' },
    });

    assert.equal(result.response.status, 404);
    assert.deepEqual(result.body, { error: 'Booking not found' });
    assert.equal(database.payments.size, 0);
    assert.equal(database.invoices.size, 0);
  });

  it('rejects payment for a booking that is not CONFIRMED', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'pending-payment-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'pending-payment-customer', status: 'PENDING' });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'Card' },
    });

    assert.equal(result.response.status, 409);
    assert.deepEqual(result.body, { error: 'Only a confirmed booking can be paid' });
    assert.equal(database.payments.size, 0);
    assert.equal(database.invoices.size, 0);
  });

  it('uses the C++ duration, rate, tax and invoice amount calculations', async () => {
    const { baseUrl, database } = await startApp(new FakePostgres(), {
      VRMS_INVOICE_TAX_PERCENT: '18',
    });
    const token = await tokenFor(database, 'calculation-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'calculation-customer' });

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'Cash' },
    });

    assert.equal(result.response.status, 201);
    assert.equal(result.body.amount, 4248);
    assert.equal(result.body.emailStatus, 'SENT');
    assert.match(result.body.emailMessage, /Invoice PDF emailed/);
    const invoice = [...database.invoices.values()][0];
    assert.equal(Number(invoice.subtotal), 3600);
    assert.equal(Number(invoice.discount), 0);
    assert.equal(Number(invoice.tax), 648);
    assert.equal(Number(invoice.total_amount), 4248);
  });

  it('defaults invoice tax to zero and rejects out-of-range tax configuration', () => {
    assert.equal(loadConfig(environment).invoiceTaxPercent, 0);
    assert.throws(() => loadConfig({
      ...environment,
      VRMS_INVOICE_TAX_PERCENT: '100.01',
    }), /VRMS_INVOICE_TAX_PERCENT/);
  });

  it('creates one invoice per booking and reuses payment and invoice on repeat requests', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'idempotency-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'idempotency-customer' });
    const body = { bookingId: 'BK-PAYMENT', method: 'UPI' };

    const first = await request(baseUrl, '/api/payments', { method: 'POST', token, body });
    const repeated = await request(baseUrl, '/api/payments', { method: 'POST', token, body });

    assert.equal(first.response.status, 201);
    assert.equal(repeated.response.status, 200);
    assert.equal(repeated.body.paymentId, first.body.paymentId);
    assert.equal(repeated.body.invoiceId, first.body.invoiceId);
    assert.equal(database.payments.size, 1);
    assert.equal(database.invoices.size, 1);
  });

  it('recovers after payment commit when invoice creation fails, without duplicate payment', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'invoice-recovery-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'invoice-recovery-customer' });
    database.failInvoiceInsert = true;

    const failedInvoice = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'Card' },
    });

    assert.equal(failedInvoice.response.status, 503);
    assert.equal(database.payments.size, 1, 'the committed simulated payment remains saved');
    assert.equal(database.invoices.size, 0);
    const savedPaymentId = [...database.payments.keys()][0];
    const savedAmount = Number(database.payments.get(savedPaymentId).amount);

    database.failInvoiceInsert = false;
    const retry = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'Cash' },
    });

    assert.equal(retry.response.status, 200);
    assert.equal(retry.body.paymentId, savedPaymentId);
    assert.equal(retry.body.amount, savedAmount);
    assert.equal(database.payments.size, 1);
    assert.equal(database.invoices.size, 1);
    assert.equal(Number([...database.invoices.values()][0].total_amount), savedAmount);
  });

  it('rolls back payment transactions and reports database errors safely', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'payment-rollback-customer');
    seedVehicle(database);
    seedBooking(database, { customerId: 'payment-rollback-customer' });
    database.failPaymentCommit = true;

    const failed = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'UPI' },
    });
    assert.equal(failed.response.status, 503);
    assert.equal(database.payments.size, 0);
    assert.equal(database.invoices.size, 0);

    database.failPaymentCommit = false;
    database.failPaymentQueries = true;
    const list = await request(baseUrl, '/api/payments', { token });
    assert.equal(list.response.status, 503);
    assert.deepEqual(list.body, { error: 'Payment service is not available' });
    assert.equal(JSON.stringify(list.body).includes('simulated payment database failure'), false);
  });

  it('does not expose an existing payment when ownership lookup fails', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'unverified-payment-owner');
    seedVehicle(database);
    seedBooking(database, { customerId: 'another-customer' });
    database.payments.set('PAY-PRIVATE', {
      payment_id: 'PAY-PRIVATE', booking_id: 'BK-PAYMENT', amount: '3600.00',
      payment_method: 'Card', payment_status: 'Successful',
    });
    database.failBookingPaymentLookup = true;

    const result = await request(baseUrl, '/api/payments', {
      method: 'POST', token,
      body: { bookingId: 'BK-PAYMENT', method: 'UPI' },
    });

    assert.equal(result.response.status, 503);
    assert.equal(result.body.paymentId, undefined);
    assert.equal(database.payments.size, 1);
  });

  it('shows customers only their payment and invoice records while admins see all', async () => {
    const { baseUrl, database } = await startApp();
    const customerToken = await tokenFor(database, 'visibility-customer');
    const adminToken = await tokenFor(database, 'visibility-admin', 'ADMIN');
    seedBooking(database, {
      bookingId: 'BK-OWNED', customerId: 'visibility-customer', vehicleId: 'CAR-OWN',
    });
    seedBooking(database, {
      bookingId: 'BK-OTHER', customerId: 'other-customer', vehicleId: 'CAR-OTHER',
    });
    database.payments.set('PAY-OWN', {
      payment_id: 'PAY-OWN', booking_id: 'BK-OWNED', amount: '25.00',
      payment_method: 'UPI', payment_status: 'Successful',
    });
    database.payments.set('PAY-OTHER', {
      payment_id: 'PAY-OTHER', booking_id: 'BK-OTHER', amount: '50.00',
      payment_method: 'Cash', payment_status: 'Successful',
    });
    database.invoices.set('INV-OWN', {
      invoice_id: 'INV-OWN', booking_id: 'BK-OWNED', subtotal: '25.00',
      discount: '0.00', tax: '0.00', total_amount: '25.00',
      email_status: 'PENDING', email_attempts: 0,
    });
    database.invoices.set('INV-OTHER', {
      invoice_id: 'INV-OTHER', booking_id: 'BK-OTHER', subtotal: '50.00',
      discount: '0.00', tax: '0.00', total_amount: '50.00',
      email_status: 'SENT', email_attempts: 1,
    });

    const ownPayments = await request(baseUrl, '/api/payments', { token: customerToken });
    const allPayments = await request(baseUrl, '/api/payments', { token: adminToken });
    const ownInvoices = await request(baseUrl, '/api/invoices', { token: customerToken });
    const allInvoices = await request(baseUrl, '/api/invoices', { token: adminToken });

    assert.deepEqual(ownPayments.body.map((payment) => payment.paymentId), ['PAY-OWN']);
    assert.deepEqual(allPayments.body.map((payment) => payment.paymentId), ['PAY-OTHER', 'PAY-OWN']);
    assert.deepEqual(ownInvoices.body.map((invoice) => invoice.invoiceId), ['INV-OWN']);
    assert.deepEqual(allInvoices.body.map((invoice) => invoice.invoiceId), ['INV-OTHER', 'INV-OWN']);
    assert.deepEqual(Object.keys(ownPayments.body[0]),
      ['paymentId', 'bookingId', 'amount', 'method', 'status']);
    assert.deepEqual(Object.keys(ownInvoices.body[0]), [
      'invoiceId', 'bookingId', 'subtotal', 'discount', 'tax', 'totalAmount', 'emailStatus', 'emailAttempts',
    ]);
  });

  it('returns safe invoice read errors', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'invoice-error-customer');
    database.failInvoiceQueries = true;

    const result = await request(baseUrl, '/api/invoices', { token });
    assert.equal(result.response.status, 503);
    assert.deepEqual(result.body, { error: 'Invoice service is not available' });
  });

  it('serializes concurrent payments for one booking without duplicate payments or invoices', async () => {
    const { baseUrl, database } = await startApp();
    const firstToken = await tokenFor(database, 'concurrent-pay-customer-a');
    seedVehicle(database);
    seedBooking(database, { customerId: 'concurrent-pay-customer-a' });
    const body = { bookingId: 'BK-PAYMENT', method: 'UPI' };

    const results = await Promise.all([
      request(baseUrl, '/api/payments', { method: 'POST', token: firstToken, body }),
      request(baseUrl, '/api/payments', { method: 'POST', token: firstToken, body }),
    ]);

    assert.deepEqual(results.map((result) => result.response.status).sort(), [200, 201]);
    assert.equal(results[0].body.paymentId, results[1].body.paymentId);
    assert.equal(results[0].body.invoiceId, results[1].body.invoiceId);
    assert.equal(database.payments.size, 1);
    assert.equal(database.invoices.size, 1);
  });
});
