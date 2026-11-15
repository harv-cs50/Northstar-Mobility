import express from 'express';
import { AuditLogRepository } from './repositories/auditLogRepository.js';
import { SessionRepository } from './repositories/sessionRepository.js';
import { UserRepository } from './repositories/userRepository.js';
import { AuthService } from './services/authService.js';
import { createAuthorizationMiddleware } from './middleware/authorization.js';
import { createAdminUserRouter, createAuthRouter } from './routes/authRoutes.js';
import { createAdminVehicleRouter, createPublicVehicleRouter } from './routes/vehicleRoutes.js';
import { createBookingRouter } from './routes/bookingRoutes.js';
import { VehicleRepository } from './repositories/vehicleRepository.js';
import { VehicleService } from './services/vehicleService.js';
import { BookingRepository } from './repositories/bookingRepository.js';
import { BookingService } from './services/bookingService.js';
import { PaymentRepository } from './repositories/paymentRepository.js';
import { InvoiceRepository } from './repositories/invoiceRepository.js';
import { PaymentService } from './services/paymentService.js';
import { InvoiceService } from './services/invoiceService.js';
import {
  createAdminInvoiceRouter,
  createInvoiceRouter,
  createPaymentRouter,
} from './routes/paymentRoutes.js';
import { EmailService } from './services/emailService.js';
import { InvoiceDeliveryService } from './services/invoiceDeliveryService.js';
import { ChatbotService } from './services/chatbotService.js';
import { GeminiClient } from './services/geminiClient.js';
import { InMemoryChatRateLimiter } from './services/chatRateLimiter.js';
import { createChatRouter } from './routes/chatRoutes.js';
import { AdminOverviewRepository } from './repositories/adminOverviewRepository.js';
import { createAdminReadRouter } from './routes/adminReadRoutes.js';

export function createApp({
  config,
  database,
  emailService = new EmailService(),
  geminiClient = new GeminiClient(),
  chatRateLimiter = new InMemoryChatRateLimiter(),
}) {
  if (!config?.allowedOrigin) {
    throw new TypeError('An allowed origin is required to create the API app');
  }
  if (!database || typeof database.query !== 'function') {
    throw new TypeError('A PostgreSQL-compatible database pool is required');
  }

  const app = express();
  app.disable('x-powered-by');

  const sessions = new SessionRepository(database);
  const audit = new AuditLogRepository(database);
  const authService = new AuthService({
    users: new UserRepository(database),
    sessions,
    audit,
    bootstrapToken: config.adminBootstrapToken ?? '',
  });
  const authorization = createAuthorizationMiddleware(sessions);
  const vehicleService = new VehicleService(new VehicleRepository(database));
  const bookingService = new BookingService(new BookingRepository(database), audit);
  const adminOverviewRepository = new AdminOverviewRepository(database);
  const invoiceService = new InvoiceService(database, {
    repository: new InvoiceRepository(database),
    audit,
  });
  const invoiceDeliveryService = new InvoiceDeliveryService({
    repository: invoiceService.repository,
    emailService,
  });
  const paymentService = new PaymentService({
    database,
    paymentRepository: new PaymentRepository(database),
    invoiceService,
    invoiceDeliveryService,
    audit,
    taxPercent: config.invoiceTaxPercent ?? 0,
  });
  const chatbotService = new ChatbotService({
    vehicleRepository: new VehicleRepository(database),
    bookingRepository: new BookingRepository(database),
    paymentRepository: new PaymentRepository(database),
    invoiceRepository: new InvoiceRepository(database),
    geminiClient,
    taxPercent: config.invoiceTaxPercent ?? 0,
  });

  app.use((request, response, next) => {
    const requestOrigin = request.get('Origin');
    response.vary('Origin');

    if (requestOrigin === config.allowedOrigin) {
      response.set('Access-Control-Allow-Origin', config.allowedOrigin);
      response.set('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      response.set(
        'Access-Control-Allow-Headers',
        'Authorization, Content-Type, X-Admin-Bootstrap-Token',
      );
    }

    if (request.method === 'OPTIONS') {
      return response.status(204).end();
    }
    return next();
  });

  app.use(express.json({ limit: '12kb', strict: true }));

  app.get('/api/health', async (request, response) => {
    try {
      await database.query('SELECT 1');
      return response.status(200).json({
        status: 'ok',
        database: { available: true },
      });
    } catch {
      return response.status(503).json({
        status: 'error',
        database: { available: false },
      });
    }
  });

  app.use('/api/vehicles', createPublicVehicleRouter(vehicleService));
  app.use('/api/chat', createChatRouter({
    chatbotService,
    authorization,
    rateLimiter: chatRateLimiter,
  }));
  app.use('/api/bookings', createBookingRouter({ bookingService, authorization }));
  app.use('/api/payments', createPaymentRouter({ paymentService, authorization }));
  app.use('/api/invoices', createInvoiceRouter({ invoiceService, authorization }));
  app.use('/api/auth', createAuthRouter({ authService, authorization }));
  app.use('/api/admin', createAdminReadRouter({
    bookingService,
    overviewRepository: adminOverviewRepository,
    authorization,
  }));
  app.use('/api/admin', createAdminUserRouter({ authService, authorization }));
  app.use('/api/admin/invoices', createAdminInvoiceRouter({
    invoiceDeliveryService,
    authorization,
  }));
  app.use('/api/admin/vehicles', createAdminVehicleRouter({ vehicleService, authorization }));

  app.use('/api', (request, response) => {
    response.status(404).json({ error: 'API route not found' });
  });

  app.use((error, request, response, next) => {
    if (response.headersSent) return next(error);

    if (error?.type === 'entity.too.large') {
      return response.status(413).json({ error: 'Request body exceeds the 12 KB limit' });
    }
    if (error instanceof SyntaxError && error?.type === 'entity.parse.failed') {
      return response.status(400).json({ error: 'Request body must be valid JSON' });
    }

    if (Number.isInteger(error?.status) && error.status >= 400 && error.status < 600) {
      return response.status(error.status).json({ error: error.message });
    }

    return response.status(500).json({ error: 'Internal server error' });
  });

  return app;
}
