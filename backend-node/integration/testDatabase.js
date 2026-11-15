import assert from 'node:assert/strict';

export const TEST_DATABASE_NAME = 'vrms_node_test';

export function loadTestDatabaseConfig(environment = process.env) {
  const user = environment.VRMS_TEST_PGUSER?.trim();
  const password = environment.VRMS_TEST_PGPASSWORD;
  if (!user || !password) {
    throw new Error('Set VRMS_TEST_PGUSER and VRMS_TEST_PGPASSWORD for the isolated PostgreSQL integration tests');
  }
  if (environment.VRMS_TEST_PGDATABASE &&
      environment.VRMS_TEST_PGDATABASE !== TEST_DATABASE_NAME) {
    throw new Error(`Integration tests only permit database ${TEST_DATABASE_NAME}`);
  }
  return {
    host: '127.0.0.1',
    port: 5432,
    database: TEST_DATABASE_NAME,
    user,
    password,
    application_name: 'northstar-vrms-integration-tests',
    max: 10,
    connectionTimeoutMillis: 3000,
  };
}

export async function assertTestDatabase(queryable) {
  const result = await queryable.query('SELECT current_database() AS database_name');
  const databaseName = result.rows[0]?.database_name;
  assert.equal(databaseName, TEST_DATABASE_NAME,
    `Refusing integration database operation outside ${TEST_DATABASE_NAME}`);
  return databaseName;
}
