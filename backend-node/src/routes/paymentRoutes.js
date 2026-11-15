import { Router } from 'express';

export function createPaymentRouter({ paymentService, authorization }) {
  const router = Router();

  router.get('/', authorization.authenticate, authorization.requireAuthenticated,
    async (request, response, next) => {
      try {
        const payments = await paymentService.getPayments(request.principal);
        return response.status(200).json(payments);
      } catch (error) {
        return next(error);
      }
    });

  router.post('/', authorization.authenticate, authorization.requireCustomer,
    async (request, response, next) => {
      try {
        const result = await paymentService.createPayment(
          request.principal.userId,
          request.body ?? {},
        );
        return response.status(result.httpStatus).json(result.body);
      } catch (error) {
        return next(error);
      }
    });

  return router;
}

export function createInvoiceRouter({ invoiceService, authorization }) {
  const router = Router();
  router.get('/', authorization.authenticate, authorization.requireAuthenticated,
    async (request, response, next) => {
      try {
        const invoices = await invoiceService.getInvoices(request.principal);
        return response.status(200).json(invoices);
      } catch {
        return next(Object.assign(new Error('Invoice service is not available'), { status: 503 }));
      }
    });
  return router;
}

export function createAdminInvoiceRouter({ invoiceDeliveryService, authorization }) {
  const router = Router();
  router.post('/:invoiceId/retry-email', authorization.authenticate,
    authorization.requireAdmin, async (request, response, next) => {
      try {
        const delivery = await invoiceDeliveryService.sendInvoice(request.params.invoiceId);
        return response.status(200).json({
          invoiceId: request.params.invoiceId,
          emailStatus: delivery.status,
          message: delivery.message,
        });
      } catch (error) {
        return next(error);
      }
    });
  return router;
}
