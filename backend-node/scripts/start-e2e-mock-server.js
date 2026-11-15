import dotenv from 'dotenv';
import { once } from 'node:events';
import { createApp } from '../src/app.js';
import { loadConfig } from '../src/config/env.js';
import { createDatabasePool } from '../src/db/pool.js';

const TEST_DATABASE = 'vrms_node_test';
const TEST_HOST = '127.0.0.1';
const TEST_PORT = 5432;

const mockEmailService = Object.freeze({
  async sendInvoice(_recipient, _invoiceId, pdfBytes) {
    const bytes = Buffer.isBuffer(pdfBytes) ? pdfBytes : Buffer.from(pdfBytes ?? []);
    if (bytes.length < 5 || bytes.subarray(0, 5).toString('ascii') !== '%PDF-') {
      throw new Error('Mock SMTP rejected a non-PDF invoice attachment');
    }
    // Accept the generated attachment locally; no message is sent to an SMTP server.
  },
});

const mockGeminiClient = Object.freeze({
  async generate() {
    return 'This is a local Gemini mock response. No external AI request was made.';
  },
});

async function startE2eMockServer() {
  dotenv.config();
  const config = loadConfig();

  if (config.database.host !== TEST_HOST ||
      config.database.port !== TEST_PORT ||
      config.database.database !== TEST_DATABASE) {
    throw new Error(
      'E2E mock server is restricted to 127.0.0.1:5432/vrms_node_test; no connection was opened',
    );
  }

  const database = createDatabasePool(config.database);
  let actualDatabase;
  try {
    const result = await database.query('SELECT current_database() AS database_name');
    actualDatabase = result.rows[0]?.database_name;
  } catch {
    await database.end().catch(() => {});
    throw new Error('Could not verify the isolated local test database; no API server was started');
  }
  if (actualDatabase !== TEST_DATABASE) {
    await database.end().catch(() => {});
    throw new Error('Connected database is not vrms_node_test; no API server was started');
  }

  const app = createApp({
    config,
    database,
    emailService: mockEmailService,
    geminiClient: mockGeminiClient,
  });
  const server = app.listen(config.port, TEST_HOST);
  try {
    await once(server, 'listening');
  } catch (error) {
  console.error('Actual E2E startup error:', error);
  await database.end().catch(() => {});
  throw new Error('The local E2E mock API could not start', { cause: error });
}

  console.log(
    `E2E mock API listening on ${TEST_HOST}:${config.port}; database verified as ${TEST_DATABASE}. SMTP and Gemini are mocked.`,
  );

  let shutdownPromise;
  function shutdown(signal) {
    if (shutdownPromise) return shutdownPromise;
    console.log(`Received ${signal}; closing the E2E mock API`);
    shutdownPromise = new Promise((resolve) => {
      server.close((error) => {
        if (error) process.exitCode = 1;
        resolve();
      });
    }).then(async () => {
      try {
        await database.end();
        console.log('E2E mock PostgreSQL pool closed');
      } catch {
        console.error('E2E mock PostgreSQL pool shutdown did not complete cleanly');
        process.exitCode = 1;
      }
    });
    return shutdownPromise;
  }

  process.once('SIGINT', () => { void shutdown('SIGINT'); });
  process.once('SIGTERM', () => { void shutdown('SIGTERM'); });
}

startE2eMockServer().catch((error) => {
  console.error(error instanceof Error ? error.message : 'E2E mock API startup failed');
  process.exitCode = 1;
});
