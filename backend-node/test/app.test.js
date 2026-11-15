import assert from 'node:assert/strict';
import { afterEach, describe, it } from 'node:test';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';

const validEnvironment = {
  PGHOST: '127.0.0.1',
  PGPORT: '5432',
  PGDATABASE: 'vrms_test',
  PGUSER: 'test_user',
  PGPASSWORD: 'test-only-placeholder',
  ALLOWED_ORIGIN: 'http://localhost:5173',
};

const servers = new Set();

async function startApp({ database, config = loadConfig(validEnvironment) }) {
  const app = createApp({ config, database });
  const server = app.listen(0, '127.0.0.1');
  servers.add(server);
  await new Promise((resolve, reject) => {
    server.once('listening', resolve);
    server.once('error', reject);
  });
  const address = server.address();
  return `http://127.0.0.1:${address.port}`;
}

afterEach(async () => {
  await Promise.all([...servers].map((server) => new Promise((resolve) => {
    server.close(() => resolve());
  })));
  servers.clear();
});

describe('GET /api/health', () => {
  it('returns database availability when PostgreSQL responds', async () => {
    let queryText;
    const baseUrl = await startApp({
      database: {
        async query(sql) {
          queryText = sql;
          return { rows: [{ '?column?': 1 }] };
        },
      },
    });

    const response = await fetch(`${baseUrl}/api/health`);
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), {
      status: 'ok',
      database: { available: true },
    });
    assert.equal(queryText, 'SELECT 1');
  });

  it('returns 503 without exposing database errors when PostgreSQL is unavailable', async () => {
    const baseUrl = await startApp({
      database: {
        async query() {
          throw new Error('private connection detail');
        },
      },
    });

    const response = await fetch(`${baseUrl}/api/health`);
    const body = await response.json();
    assert.equal(response.status, 503);
    assert.deepEqual(body, {
      status: 'error',
      database: { available: false },
    });
    assert.equal(JSON.stringify(body).includes('private connection detail'), false);
  });
});

describe('environment configuration', () => {
  it('rejects missing required environment variables without echoing values', () => {
    assert.throws(
      () => loadConfig({ PGPASSWORD: 'must-not-be-echoed' }),
      (error) => {
        assert.match(error.message, /PGHOST/);
        assert.match(error.message, /PGPORT/);
        assert.equal(error.message.includes('must-not-be-echoed'), false);
        return true;
      },
    );
  });

  it('rejects invalid ports and non-exact frontend origins', () => {
    assert.throws(() => loadConfig({
      ...validEnvironment,
      PGPORT: '70000',
    }), /PGPORT/);
    assert.throws(() => loadConfig({
      ...validEnvironment,
      ALLOWED_ORIGIN: 'https://northstar.example/app',
    }), /ALLOWED_ORIGIN/);
  });

  it('maps validated PostgreSQL settings and applies the API port default', () => {
    const config = loadConfig(validEnvironment);
    assert.equal(config.port, 8080);
    assert.deepEqual(config.database, {
      host: validEnvironment.PGHOST,
      port: 5432,
      database: validEnvironment.PGDATABASE,
      user: validEnvironment.PGUSER,
      password: validEnvironment.PGPASSWORD,
      ssl: false,
      application_name: 'northstar-vrms-api',
      max: 10,
    });
  });

  it('requires certificate verification for production PostgreSQL connections', () => {
    const config = loadConfig({
      ...validEnvironment,
      NODE_ENV: 'production',
    });

    assert.deepEqual(config.database.ssl, { rejectUnauthorized: true });
  });
});

describe('HTTP middleware', () => {
  it('allows only the configured exact CORS origin', async () => {
    const baseUrl = await startApp({ database: { async query() { return { rows: [] }; } } });

    const allowed = await fetch(`${baseUrl}/api/health`, {
      headers: { Origin: validEnvironment.ALLOWED_ORIGIN },
    });
    const disallowed = await fetch(`${baseUrl}/api/health`, {
      headers: { Origin: 'https://attacker.example' },
    });

    assert.equal(allowed.headers.get('access-control-allow-origin'), validEnvironment.ALLOWED_ORIGIN);
    assert.equal(disallowed.headers.get('access-control-allow-origin'), null);
  });

  it('returns centralized JSON errors for malformed and oversized JSON', async () => {
    const baseUrl = await startApp({ database: { async query() { return { rows: [] }; } } });

    const malformed = await fetch(`${baseUrl}/api/health`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{invalid',
    });
    assert.equal(malformed.status, 400);
    assert.deepEqual(await malformed.json(), { error: 'Request body must be valid JSON' });

    const oversized = await fetch(`${baseUrl}/api/health`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ content: 'x'.repeat(13 * 1024) }),
    });
    assert.equal(oversized.status, 413);
    assert.deepEqual(await oversized.json(), { error: 'Request body exceeds the 12 KB limit' });
  });

  it('returns JSON for unknown API routes', async () => {
    const baseUrl = await startApp({ database: { async query() { return { rows: [] }; } } });
    const response = await fetch(`${baseUrl}/api/not-implemented`);
    assert.equal(response.status, 404);
    assert.deepEqual(await response.json(), { error: 'API route not found' });
  });
});
