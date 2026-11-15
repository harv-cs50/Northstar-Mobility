import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { loadTestDatabaseConfig, TEST_DATABASE_NAME } from '../integration/testDatabase.js';

describe('PostgreSQL integration database guard', () => {
  it('pins the integration pool to localhost and vrms_node_test regardless of PGDATABASE', () => {
    const config = loadTestDatabaseConfig({
      VRMS_TEST_PGUSER: 'integration-test-user',
      VRMS_TEST_PGPASSWORD: 'test-only-placeholder',
      PGDATABASE: 'vehicle_rental_db',
      PGHOST: 'production-host.example.test',
    });
    assert.equal(config.host, '127.0.0.1');
    assert.equal(config.port, 5432);
    assert.equal(config.database, TEST_DATABASE_NAME);
    assert.notEqual(config.database, 'vehicle_rental_db');
  });

  it('rejects an attempted test database override to vehicle_rental_db', () => {
    assert.throws(() => loadTestDatabaseConfig({
      VRMS_TEST_PGUSER: 'integration-test-user',
      VRMS_TEST_PGPASSWORD: 'test-only-placeholder',
      VRMS_TEST_PGDATABASE: 'vehicle_rental_db',
    }), /only permit database vrms_node_test/);
  });
});
