import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const validEnvironment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_vehicle_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const servers = new Set();

async function startApp(database = new FakePostgres()) {
  const app = createApp({ config: loadConfig(validEnvironment), database });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return { baseUrl: `http://127.0.0.1:${server.address().port}`, database };
}

async function authToken(database, userId, role) {
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

const vehicleDetails = {
  id: 'CAR-100',
  brand: 'Toyota',
  model: 'Corolla',
  type: 'Car',
  dailyRate: 1250.5,
};

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(() => resolve());
  })));
  servers.clear();
});

describe('vehicle API', () => {
  it('public listing returns the C++ vehicle format and excludes unavailable vehicles', async () => {
    const { baseUrl, database } = await startApp();
    database.seedVehicle({
      vehicle_id: 'CAR-001', brand: 'Honda', model: 'City', vehicle_type: 'Car',
      daily_rate: '1800.00', is_available: true,
    });
    database.seedVehicle({
      vehicle_id: 'CAR-002', brand: 'Suzuki', model: 'Swift', vehicle_type: 'Car',
      daily_rate: '1500.00', is_available: false,
    });

    const result = await request(baseUrl, '/api/vehicles');
    assert.equal(result.response.status, 200);
    assert.deepEqual(result.body, [{
      id: 'CAR-001',
      brand: 'Honda',
      model: 'City',
      type: 'Car',
      dailyRate: 1800,
      available: true,
    }]);
    assert.deepEqual(Object.keys(result.body[0]),
      ['id', 'brand', 'model', 'type', 'dailyRate', 'available']);
  });

  it('admin listing includes both available and unavailable vehicles', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-vehicle-list', 'ADMIN');
    database.seedVehicle({
      vehicle_id: 'CAR-A', brand: 'Honda', model: 'City', vehicle_type: 'Car',
      daily_rate: '1800.00', is_available: true,
    });
    database.seedVehicle({
      vehicle_id: 'CAR-B', brand: 'Suzuki', model: 'Swift', vehicle_type: 'Car',
      daily_rate: '1500.00', is_available: false,
    });

    const result = await request(baseUrl, '/api/admin/vehicles', { token });
    assert.equal(result.response.status, 200);
    assert.deepEqual(result.body.map((vehicle) => vehicle.id), ['CAR-A', 'CAR-B']);
    assert.equal(result.body[1].available, false);
  });

  it('denies customers access to both admin vehicle endpoints', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'customer-vehicle-denied', 'CUSTOMER');
    const list = await request(baseUrl, '/api/admin/vehicles', { token });
    const create = await request(baseUrl, '/api/admin/vehicles', {
      method: 'POST', token, body: vehicleDetails,
    });

    assert.equal(list.response.status, 403);
    assert.equal(create.response.status, 403);
    assert.deepEqual(list.body, { error: 'Administrator account required' });
    assert.deepEqual(create.body, { error: 'Administrator account required' });
    assert.equal(database.vehicles.size, 0);
  });

  it('creates a vehicle for an administrator and returns the existing success format', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-vehicle-create', 'ADMIN');
    const result = await request(baseUrl, '/api/admin/vehicles', {
      method: 'POST', token, body: { ...vehicleDetails, role: 'CUSTOMER' },
    });

    assert.equal(result.response.status, 201);
    assert.deepEqual(result.body, { success: true });
    assert.deepEqual(database.vehicles.get(vehicleDetails.id), {
      vehicle_id: vehicleDetails.id,
      brand: vehicleDetails.brand,
      model: vehicleDetails.model,
      vehicle_type: vehicleDetails.type,
      daily_rate: '1250.5',
      is_available: true,
    });
  });

  it('rejects duplicate vehicle IDs without replacing the existing vehicle', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-vehicle-duplicate', 'ADMIN');
    database.seedVehicle({
      vehicle_id: vehicleDetails.id,
      brand: 'Existing',
      model: 'Vehicle',
      vehicle_type: 'Car',
      daily_rate: '999.00',
      is_available: false,
    });

    const result = await request(baseUrl, '/api/admin/vehicles', {
      method: 'POST', token, body: vehicleDetails,
    });
    assert.equal(result.response.status, 409);
    assert.deepEqual(result.body, { error: 'Vehicle ID already exists' });
    assert.equal(database.vehicles.get(vehicleDetails.id).brand, 'Existing');
  });

  it('rejects invalid and non-positive daily rates', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-invalid-rates', 'ADMIN');
    for (const invalidRate of [0, -1, '0.001', 10_000_000_000, 'NaN', 'Infinity', 'not-a-rate', null]) {
      const vehicleId = `BAD-${String(invalidRate)}`;
      const result = await request(baseUrl, '/api/admin/vehicles', {
        method: 'POST', token, body: { ...vehicleDetails, id: vehicleId, dailyRate: invalidRate },
      });
      assert.equal(result.response.status, 400, `dailyRate=${String(invalidRate)}`);
      assert.equal(database.vehicles.has(vehicleId), false);
    }
  });

  it('rejects missing or empty vehicle fields', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-invalid-fields', 'ADMIN');
    const result = await request(baseUrl, '/api/admin/vehicles', {
      method: 'POST', token, body: { ...vehicleDetails, brand: '' },
    });
    assert.equal(result.response.status, 400);
    assert.deepEqual(result.body, { error: 'Invalid vehicle details' });
    assert.equal(database.vehicles.size, 0);
  });

  it('returns safe service errors when PostgreSQL operations fail', async () => {
    const { baseUrl, database } = await startApp();
    const token = await authToken(database, 'admin-vehicle-db-error', 'ADMIN');
    database.failVehicleQueries = true;

    const publicList = await request(baseUrl, '/api/vehicles');
    const adminCreate = await request(baseUrl, '/api/admin/vehicles', {
      method: 'POST', token, body: vehicleDetails,
    });

    assert.equal(publicList.response.status, 503);
    assert.equal(adminCreate.response.status, 503);
    assert.deepEqual(publicList.body, { error: 'Vehicle service is not available' });
    assert.deepEqual(adminCreate.body, { error: 'Vehicle service is not available' });
    assert.equal(JSON.stringify(publicList.body).includes('private vehicle database error detail'), false);
  });
});
