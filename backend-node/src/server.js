import dotenv from 'dotenv';
import { createApp } from './app.js';
import { loadConfig } from './config/env.js';
import { createDatabasePool } from './db/pool.js';

dotenv.config();

const config = loadConfig();
const database = createDatabasePool(config.database);
const app = createApp({ config, database });
const server = app.listen(config.port, process.env.HOST || '127.0.0.1', () => {
  console.log(`Northstar API listening on port ${config.port}`);
});

let shutdownPromise;

function shutdown(signal) {
  if (shutdownPromise) return shutdownPromise;

  console.log(`Received ${signal}; closing the API server`);
  shutdownPromise = new Promise((resolve) => {
    server.close((error) => {
      if (error) {
        console.error('HTTP server shutdown did not complete cleanly');
        process.exitCode = 1;
      }
      resolve();
    });
  }).then(async () => {
    try {
      await database.end();
      console.log('PostgreSQL pool closed');
    } catch {
      console.error('PostgreSQL pool shutdown did not complete cleanly');
      process.exitCode = 1;
    }
  });

  return shutdownPromise;
}

process.once('SIGINT', () => { void shutdown('SIGINT'); });
process.once('SIGTERM', () => { void shutdown('SIGTERM'); });
