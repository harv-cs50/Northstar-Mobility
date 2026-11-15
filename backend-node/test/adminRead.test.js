import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const environment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_fake_admin_read_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};
const servers = new Set();

async function startApp() {
  const database = new FakePostgres();
  const originalQuery = database.query.bind(database);
  database.query = async (sql, values = []) => {
    const statement = sql.replace(/\s+/g, ' ').trim();
    if (statement.includes('FROM maintenance_records ORDER BY service_date DESC')) {
      return { rows: [{
        maintenance_id: 'M-1', vehicle_id: 'V-1', service_date: '2026-10-01',
        description: 'Routine service', status: 'SCHEDULED', maintenance_cost: '250.00',
        completion_date: null,
      }] };
    }
    if (statement.includes('FROM rentals ORDER BY rental_id')) {
      return { rows: [
        {
          rental_id: 'R-1', booking_id: 'B-1', planned_return_date: '2026-10-05',
          actual_return_date: null, daily_late_fee: '50.00',
        },
        {
          rental_id: 'R-2', booking_id: 'B-2', planned_return_date: '2026-10-06',
          actual_return_date: '2026-10-06', daily_late_fee: '75.00',
        },
      ] };
    }
    if (statement.includes('FROM bookings ORDER BY start_date DESC, booking_id')) {
      return { rows: [{
        booking_id: 'B-1', customer_id: 'C-1', vehicle_id: 'V-1',
        start_date: '2026-10-01', end_date: '2026-10-03', status: 'CONFIRMED',
      }] };
    }
    return originalQuery(sql, values);
  };

  const app = createApp({ config: loadConfig(environment), database });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return { baseUrl: `http://127.0.0.1:${server.address().port}`, database };
}

async function tokenFor(database, userId, role) {
  database.seedUser({ user_id: userId, role });
  return new SessionRepository(database).createSession(userId, role);
}

async function get(baseUrl, path, token) {
  const response = await fetch(`${baseUrl}${path}`, {
    headers: token ? { Authorization: `Bearer ${token}` } : {},
  });
  return { response, body: await response.json() };
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => server.close(resolve))));
  servers.clear();
});

describe('admin read APIs used by the existing dashboard', () => {
  it('preserves the C++ admin booking, maintenance, and rental response shapes', async () => {
    const { baseUrl, database } = await startApp();
    const token = await tokenFor(database, 'admin-read', 'ADMIN');

    const bookings = await get(baseUrl, '/api/admin/bookings', token);
    assert.equal(bookings.response.status, 200);
    assert.deepEqual(bookings.body, [{
      bookingId: 'B-1', customerId: 'C-1', vehicleId: 'V-1',
      startDate: '2026-10-01', endDate: '2026-10-03', status: 'CONFIRMED',
    }]);

    const maintenance = await get(baseUrl, '/api/admin/maintenance', token);
    assert.equal(maintenance.response.status, 200);
    assert.deepEqual(maintenance.body, [{
      maintenanceId: 'M-1', vehicleId: 'V-1', serviceDate: '2026-10-01',
      description: 'Routine service', status: 'SCHEDULED', maintenanceCost: 250,
      completionDate: '',
    }]);

    const rentals = await get(baseUrl, '/api/admin/rentals', token);
    assert.equal(rentals.response.status, 200);
    assert.deepEqual(rentals.body, [
      {
        rentalId: 'R-1', bookingId: 'B-1', plannedReturnDate: '2026-10-05',
        actualReturnDate: '', dailyLateFee: 50, returned: false,
      },
      {
        rentalId: 'R-2', bookingId: 'B-2', plannedReturnDate: '2026-10-06',
        actualReturnDate: '2026-10-06', dailyLateFee: 75, returned: true,
      },
    ]);
  });

  it('denies customers and anonymous visitors on every admin listing', async () => {
    const { baseUrl, database } = await startApp();
    const customerToken = await tokenFor(database, 'customer-read', 'CUSTOMER');
    for (const path of ['/api/admin/bookings', '/api/admin/maintenance', '/api/admin/rentals']) {
      assert.equal((await get(baseUrl, path, customerToken)).response.status, 403);
      assert.equal((await get(baseUrl, path)).response.status, 401);
    }
  });
});
