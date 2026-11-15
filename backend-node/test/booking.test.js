import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const environment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_booking_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const servers = new Set();

async function startApp(database = new FakePostgres()) {
  const app = createApp({ config: loadConfig(environment), database });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return { baseUrl: `http://127.0.0.1:${server.address().port}`, database };
}

async function tokenFor(database, userId, role = 'CUSTOMER') {
  database.seedUser({ user_id: userId, role });
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

function seedVehicle(database, vehicleId = 'CAR-BOOKING') {
  database.seedVehicle({
    vehicle_id: vehicleId,
    brand: 'Toyota',
    model: 'Corolla',
    vehicle_type: 'Car',
    daily_rate: '100.00',
    is_available: true,
  });
}

function seedBooking(database, {
  bookingId = 'BK-EXISTING',
  customerId = 'customer-existing',
  vehicleId = 'CAR-BOOKING',
  startDate = '2026-04-10',
  endDate = '2026-04-12',
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

describe('booking API', () => {
  it('creates a valid booking for the authenticated customer with the C++ response fields', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-valid-booking');
    seedVehicle(database);

    const result = await request(baseUrl, '/api/bookings', {
      method: 'POST',
      token,
      body: {
        vehicleId: 'CAR-BOOKING',
        startDate: '2026-04-13',
        endDate: '2026-04-15',
        customerId: 'attacker-controlled',
      },
    });

    assert.equal(result.response.status, 201);
    assert.deepEqual(Object.keys(result.body), [
      'bookingId', 'customerId', 'vehicleId', 'startDate', 'endDate', 'status',
    ]);
    assert.equal(result.body.customerId, 'customer-valid-booking');
    assert.equal(result.body.status, 'CONFIRMED');
    assert.equal(database.bookings.size, 1);
    assert.equal(database.auditRows.some((row) =>
      row.userId === 'customer-valid-booking' && row.action === 'BOOKING_CREATED'), true);
  });

  it('rejects malformed, impossible, year-zero, and reversed booking dates without partial records', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-invalid-dates');
    seedVehicle(database);
    const invalidInputs = [
      { startDate: '2026-4-01', endDate: '2026-04-02' },
      { startDate: '2026-02-30', endDate: '2026-03-02' },
      { startDate: '0000-12-31', endDate: '0001-01-01' },
      { startDate: '2026-04-03', endDate: '2026-04-02' },
    ];

    for (const dates of invalidInputs) {
      const result = await request(baseUrl, '/api/bookings', {
        method: 'POST', token,
        body: { vehicleId: 'CAR-BOOKING', ...dates },
      });
      assert.equal(result.response.status, 400, JSON.stringify(dates));
      assert.equal(database.bookings.size, 0, 'invalid request must not insert a booking');
    }
    assert.equal(database.auditRows.filter((row) => row.action === 'BOOKING_CREATE_FAILED').length,
      invalidInputs.length);
  });

  it('treats date ranges as inclusive when detecting overlap', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-inclusive-overlap');
    seedVehicle(database);
    seedBooking(database, { startDate: '2026-04-10', endDate: '2026-04-12' });

    const result = await request(baseUrl, '/api/bookings', {
      method: 'POST', token,
      body: { vehicleId: 'CAR-BOOKING', startDate: '2026-04-12', endDate: '2026-04-14' },
    });

    assert.equal(result.response.status, 409);
    assert.equal(database.bookings.size, 1, 'conflicting request must not insert a booking');
  });

  it('allows a non-overlapping booking and ignores cancelled bookings for availability', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-cancelled-booking');
    seedVehicle(database);
    seedBooking(database, { status: 'CANCELLED' });

    const result = await request(baseUrl, '/api/bookings', {
      method: 'POST', token,
      body: { vehicleId: 'CAR-BOOKING', startDate: '2026-04-10', endDate: '2026-04-12' },
    });

    assert.equal(result.response.status, 201);
    assert.equal(database.bookings.size, 2);
  });

  it('blocks overlapping PENDING and CONFIRMED bookings', async () => {
    for (const status of ['PENDING', 'CONFIRMED']) {
      const { baseUrl, database } = await startApp();
      const token = await tokenFor(database, `customer-blocked-${status.toLowerCase()}`);
      seedVehicle(database);
      seedBooking(database, { status });

      const result = await request(baseUrl, '/api/bookings', {
        method: 'POST', token,
        body: { vehicleId: 'CAR-BOOKING', startDate: '2026-04-11', endDate: '2026-04-11' },
      });

      assert.equal(result.response.status, 409, `${status} must block overlap`);
      assert.equal(database.bookings.size, 1);
    }
  });

  it('rolls back a staged booking when the transaction commit fails', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-booking-rollback');
    seedVehicle(database);
    database.failBookingCommit = true;

    const result = await request(baseUrl, '/api/bookings', {
      method: 'POST', token,
      body: { vehicleId: 'CAR-BOOKING', startDate: '2026-04-13', endDate: '2026-04-14' },
    });

    assert.equal(result.response.status, 503);
    assert.deepEqual(result.body, { error: 'Booking service is not available' });
    assert.equal(database.bookings.size, 0, 'rollback must discard the staged booking');
  });

  it('lists only a customer’s own bookings while administrators see all bookings', async () => {
    const { baseUrl, database } = await startApp();
    const customerToken = await tokenFor(database, 'customer-booking-list');
    const adminToken = await tokenFor(database, 'admin-booking-list', 'ADMIN');
    seedBooking(database, { bookingId: 'BK-OWN', customerId: 'customer-booking-list' });
    seedBooking(database, {
      bookingId: 'BK-OTHER', customerId: 'other-customer',
      vehicleId: 'CAR-OTHER', startDate: '2026-05-10', endDate: '2026-05-11',
    });

    const own = await request(baseUrl, '/api/bookings', { token: customerToken });
    const all = await request(baseUrl, '/api/bookings', { token: adminToken });
    assert.deepEqual(own.body.map((booking) => booking.bookingId), ['BK-OWN']);
    assert.deepEqual(all.body.map((booking) => booking.bookingId), ['BK-OTHER', 'BK-OWN']);
    assert.equal(own.body[0].startDate, '2026-04-10');
  });

  it('returns 404 when a customer tries to cancel another customer’s booking', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'customer-wrong-owner');
    seedBooking(database);

    const result = await request(baseUrl, '/api/bookings/BK-EXISTING/cancel', {
      method: 'POST', token,
    });

    assert.equal(result.response.status, 404);
    assert.deepEqual(result.body, { error: 'Booking not found' });
    assert.equal(database.bookings.get('BK-EXISTING').status, 'CONFIRMED');
  });

  it('allows an administrator to cancel any booking', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'admin-booking-cancel', 'ADMIN');
    seedBooking(database);

    const result = await request(baseUrl, '/api/bookings/BK-EXISTING/cancel', {
      method: 'POST', token,
    });

    assert.equal(result.response.status, 200);
    assert.deepEqual(result.body, { success: true });
    assert.equal(database.bookings.get('BK-EXISTING').status, 'CANCELLED');
  });

  it('rejects simultaneous overlapping requests so exactly one booking succeeds', async () => {
    const { baseUrl, database } = await startApp();
    const firstToken = await tokenFor(database, 'customer-concurrent-one');
    const secondToken = await tokenFor(database, 'customer-concurrent-two');
    seedVehicle(database);
    const body = { vehicleId: 'CAR-BOOKING', startDate: '2026-06-01', endDate: '2026-06-03' };

    const results = await Promise.all([
      request(baseUrl, '/api/bookings', { method: 'POST', token: firstToken, body }),
      request(baseUrl, '/api/bookings', { method: 'POST', token: secondToken, body }),
    ]);

    assert.deepEqual(results.map((result) => result.response.status).sort(), [201, 409]);
    assert.equal(results.filter((result) => result.response.status === 201).length, 1);
    assert.equal(database.bookings.size, 1, 'only the committed request may leave a booking');
  });

  it('requires authentication and customer role to create bookings', async () => {
    const { baseUrl, database } = await startApp();
    const adminToken = await tokenFor(database, 'admin-cannot-book', 'ADMIN');
    seedVehicle(database);
    const body = { vehicleId: 'CAR-BOOKING', startDate: '2026-07-01', endDate: '2026-07-02' };

    const anonymous = await request(baseUrl, '/api/bookings', { method: 'POST', body });
    const admin = await request(baseUrl, '/api/bookings', {
      method: 'POST', token: adminToken, body,
    });
    assert.equal(anonymous.response.status, 401);
    assert.equal(admin.response.status, 403);
    assert.equal(database.bookings.size, 0);
  });
});
