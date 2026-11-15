import assert from 'node:assert/strict';
import express from 'express';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { createAuthorizationMiddleware } from '../src/middleware/authorization.js';
import { hashSessionToken, SessionRepository } from '../src/repositories/sessionRepository.js';
import { FakePostgres } from '../test-support/fakePostgres.js';

const bootstrapToken = 'test-bootstrap-token-do-not-use-outside-tests';
const password = 'Northstar-Auth-Test-Password';
const validEnvironment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_node_auth_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
  VRMS_ADMIN_BOOTSTRAP_TOKEN: bootstrapToken,
};

const servers = new Set();

async function startApp(database = new FakePostgres()) {
  const app = createApp({
    config: loadConfig(validEnvironment),
    database,
  });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  return { baseUrl: `http://127.0.0.1:${server.address().port}`, database };
}

async function request(baseUrl, path, { method = 'GET', body, token, headers = {} } = {}) {
  const response = await fetch(`${baseUrl}${path}`, {
    method,
    headers: {
      ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
      ...headers,
    },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  return { response, body: await response.json() };
}

async function register(baseUrl, overrides = {}) {
  return request(baseUrl, '/api/auth/register', {
    method: 'POST',
    body: {
      userId: 'customer-1',
      name: 'Test Customer',
      email: 'customer@example.test',
      password,
      ...overrides,
    },
  });
}

async function login(baseUrl, email = 'customer@example.test', passwordValue = password) {
  return request(baseUrl, '/api/auth/login', {
    method: 'POST',
    body: { email, password: passwordValue },
  });
}

async function bootstrap(baseUrl, details, token = bootstrapToken) {
  return request(baseUrl, '/api/auth/bootstrap-admin', {
    method: 'POST',
    body: details,
    ...(token ? { headers: { 'X-Admin-Bootstrap-Token': token } } : {}),
  });
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(() => resolve());
  })));
  servers.clear();
});

describe('authentication API', () => {
  it('registers customers with Argon2id and ignores a client-supplied role', async () => {
    const { baseUrl, database } = await startApp();
    const registration = await register(baseUrl, { role: 'ADMIN' });

    assert.equal(registration.response.status, 201);
    assert.deepEqual(registration.body, {
      userId: 'customer-1',
      name: 'Test Customer',
      role: 'CUSTOMER',
      message: 'Customer registered successfully',
    });
    const user = database.users.get('customer-1');
    assert.equal(user.role, 'CUSTOMER');
    assert.match(user.password_hash,
      /^\$argon2id\$v=19\$m=65536,t=3,p=1\$/);

    const loginResult = await login(baseUrl);
    assert.equal(loginResult.response.status, 200);
    assert.deepEqual(Object.keys(loginResult.body).sort(),
      ['name', 'role', 'sessionId', 'userId'].sort());
    assert.equal(loginResult.body.userId, 'customer-1');
    assert.equal(loginResult.body.role, 'CUSTOMER');
    assert.match(loginResult.body.sessionId, /^[0-9a-f]{64}$/);
    assert.equal(database.sessions.size, 1);
    assert.equal(database.sessions.has(hashSessionToken(loginResult.body.sessionId)), true);
    assert.equal(database.sessions.has(loginResult.body.sessionId), false);
    const auditText = JSON.stringify(database.auditRows);
    assert.equal(auditText.includes(password), false);
    assert.equal(auditText.includes(loginResult.body.sessionId), false);
  });

  it('verifies a PHC Argon2id hash with the parameters used by the C++ hasher', async () => {
    const { baseUrl, database } = await startApp();
    database.seedUser({
      user_id: 'legacy-customer',
      name: 'Legacy Customer',
      email: 'legacy@example.test',
      role: 'CUSTOMER',
      password_hash: '$argon2id$v=19$m=65536,t=3,p=1$MDEyMzQ1Njc4OWFiY2RlZg$RgdsT8Qdl+viWOUV0A0UzZUz/nm+e9dDRugcYmJmnqI',
    });

    const result = await login(baseUrl, 'legacy@example.test', 'Cxx-Compatible-Password-42');
    assert.equal(result.response.status, 200);
    assert.equal(result.body.userId, 'legacy-customer');
    assert.equal(result.body.role, 'CUSTOMER');
  });

  it('uses the same invalid-credentials response for missing users and wrong passwords', async () => {
    const { baseUrl } = await startApp();
    await register(baseUrl);

    const wrongPassword = await login(baseUrl, 'customer@example.test', 'wrong-password');
    const missingUser = await login(baseUrl, 'missing@example.test', password);
    assert.equal(wrongPassword.response.status, 401);
    assert.equal(missingUser.response.status, 401);
    assert.deepEqual(wrongPassword.body, { error: 'Invalid email or password' });
    assert.deepEqual(missingUser.body, { error: 'Invalid email or password' });
  });

  it('validates the bootstrap token and permits only the first administrator', async () => {
    const { baseUrl, database } = await startApp();
    const details = {
      userId: 'initial-admin',
      name: 'Initial Admin',
      email: 'admin@example.test',
      password,
    };

    const missing = await bootstrap(baseUrl, details, '');
    const incorrect = await bootstrap(baseUrl, details, 'incorrect-bootstrap-token');
    assert.equal(missing.response.status, 403);
    assert.equal(incorrect.response.status, 403);

    const allowed = await bootstrap(baseUrl, { ...details, role: 'CUSTOMER' });
    assert.equal(allowed.response.status, 201);
    assert.equal(allowed.body.role, 'ADMIN');
    assert.match(allowed.body.sessionId, /^[0-9a-f]{64}$/);

    const second = await bootstrap(baseUrl, {
      ...details,
      userId: 'second-admin',
      email: 'second-admin@example.test',
    });
    assert.equal(second.response.status, 409);
    assert.deepEqual(second.body, { error: 'Initial admin has already been provisioned' });
    const auditText = JSON.stringify(database.auditRows);
    assert.equal(auditText.includes(bootstrapToken), false);
    assert.equal(auditText.includes(password), false);
  });

  it('serializes concurrent initial administrator provisioning', async () => {
    const { baseUrl, database } = await startApp();
    const requests = [
      bootstrap(baseUrl, {
        userId: 'concurrent-admin-a',
        name: 'Concurrent Admin A',
        email: 'admin-a@example.test',
        password,
      }),
      bootstrap(baseUrl, {
        userId: 'concurrent-admin-b',
        name: 'Concurrent Admin B',
        email: 'admin-b@example.test',
        password,
      }),
    ];
    const results = await Promise.all(requests);
    const statuses = results.map(({ response }) => response.status).sort();

    assert.deepEqual(statuses, [201, 409]);
    assert.equal([...database.users.values()].filter((user) => user.role === 'ADMIN').length, 1);
    assert.equal(database.sessions.size, 1);
  });

  it('restricts admin user creation to an authenticated administrator', async () => {
    const { baseUrl } = await startApp();
    const details = {
      userId: 'new-admin',
      name: 'New Admin',
      email: 'new-admin@example.test',
      password,
    };

    const guest = await request(baseUrl, '/api/admin/users', { method: 'POST', body: details });
    assert.equal(guest.response.status, 401);

    await register(baseUrl);
    const customer = await login(baseUrl);
    const customerAttempt = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: details, token: customer.body.sessionId,
    });
    assert.equal(customerAttempt.response.status, 403);
    assert.deepEqual(customerAttempt.body, { error: 'Administrator account required' });

    const initialAdmin = await bootstrap(baseUrl, {
      userId: 'initial-admin', name: 'Initial Admin',
      email: 'admin@example.test', password,
    });
    const authorized = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: { ...details, role: 'CUSTOMER' }, token: initialAdmin.body.sessionId,
    });
    assert.equal(authorized.response.status, 201);
    assert.deepEqual(authorized.body, { userId: 'new-admin', role: 'ADMIN' });
  });

  it('invalidates sessions on logout and enforces sliding idle expiration', async () => {
    const { baseUrl, database } = await startApp();
    await register(baseUrl);
    const loggedIn = await login(baseUrl);
    const token = loggedIn.body.sessionId;

    const loggedOut = await request(baseUrl, '/api/auth/logout', {
      method: 'POST', token,
    });
    assert.equal(loggedOut.response.status, 200);
    assert.deepEqual(loggedOut.body, { success: true });
    const afterLogout = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: {}, token,
    });
    assert.equal(afterLogout.response.status, 401);

    const renewedLogin = await login(baseUrl);
    const renewedToken = renewedLogin.body.sessionId;
    const tokenHash = hashSessionToken(renewedToken);
    database.advanceTime(29 * 60 * 1000);

    const validAt29 = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: {}, token: renewedToken,
    });
    assert.equal(validAt29.response.status, 403);
    assert.equal(database.sessions.get(tokenHash).expires_at, database.clock + 30 * 60 * 1000);

    database.advanceTime(29 * 60 * 1000);
    const validAfterTouch = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: {}, token: renewedToken,
    });
    assert.equal(validAfterTouch.response.status, 403);

    database.advanceTime(31 * 60 * 1000);
    const expired = await request(baseUrl, '/api/admin/users', {
      method: 'POST', body: {}, token: renewedToken,
    });
    assert.equal(expired.response.status, 401);
  });
});

describe('authorization middleware roles', () => {
  it('supports authenticated, customer-only and admin-only route guards', async () => {
    const database = new FakePostgres();
    const sessions = new SessionRepository(database);
    const authorization = createAuthorizationMiddleware(sessions);
    const app = express();
    app.get('/authenticated', authorization.authenticate,
      authorization.requireAuthenticated, (request, response) => response.json({ ok: true }));
    app.get('/customer', authorization.authenticate,
      authorization.requireCustomer, (request, response) => response.json({ ok: true }));
    app.get('/admin', authorization.authenticate,
      authorization.requireAdmin, (request, response) => response.json({ ok: true }));
    app.use((error, request, response, next) => response.status(500).json({ error: 'Internal server error' }));

    const server = app.listen(0, '127.0.0.1');
    servers.add(server);
    await new Promise((resolve, reject) => {
      server.once('listening', resolve);
      server.once('error', reject);
    });
    const baseUrl = `http://127.0.0.1:${server.address().port}`;
    database.seedUser({ user_id: 'guard-customer', role: 'CUSTOMER' });
    database.seedUser({ user_id: 'guard-admin', role: 'ADMIN' });
    const customerToken = await sessions.createSession('guard-customer', 'CUSTOMER');
    const adminToken = await sessions.createSession('guard-admin', 'ADMIN');

    const guest = await fetch(`${baseUrl}/authenticated`);
    const customer = await fetch(`${baseUrl}/customer`, {
      headers: { Authorization: `Bearer ${customerToken}` },
    });
    const customerDenied = await fetch(`${baseUrl}/admin`, {
      headers: { Authorization: `Bearer ${customerToken}` },
    });
    const admin = await fetch(`${baseUrl}/admin`, {
      headers: { Authorization: `Bearer ${adminToken}` },
    });

    assert.equal(guest.status, 401);
    assert.equal(customer.status, 200);
    assert.equal(customerDenied.status, 403);
    assert.equal(admin.status, 200);
  });
});
