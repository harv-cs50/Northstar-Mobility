import { randomBytes } from 'node:crypto';
import { withTransaction } from '../repositories/transaction.js';
import { PaymentRepository } from '../repositories/paymentRepository.js';
import { InvoiceService } from './invoiceService.js';

const supportedMethods = new Set(['Card', 'UPI', 'Cash']);

export class PaymentError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function emailMessage(status) {
  if (status === 'SENT') return 'Invoice was already emailed to the registered email address.';
  if (status === 'FAILED') return 'Invoice is saved; email delivery failed. An administrator can retry it.';
  if (status === 'SENDING') return 'Invoice email delivery is already in progress.';
  return 'Invoice email delivery is pending.';
}

function paymentUnavailable() {
  return new PaymentError(
    503,
    'Payment or invoice could not be completed; any saved payment remains available for safe recovery.',
  );
}

export class PaymentService {
  constructor({
    database,
    paymentRepository = new PaymentRepository(database),
    invoiceService = new InvoiceService(database),
    invoiceDeliveryService = null,
    audit = null,
    taxPercent = 0,
  }) {
    this.database = database;
    this.paymentRepository = paymentRepository;
    this.invoiceService = invoiceService;
    this.invoiceDeliveryService = invoiceDeliveryService;
    this.audit = audit;
    this.taxPercent = taxPercent;
  }

  async recordAudit(userId, action, description) {
    try {
      await this.audit?.write(userId, action, description);
    } catch {
      // Match the C++ payment flow: audit storage errors do not reverse payment state.
    }
  }

  async getPayments(principal) {
    try {
      return principal.role === 'ADMIN'
        ? await this.paymentRepository.getAllPayments()
        : await this.paymentRepository.getCustomerPayments(principal.userId);
    } catch {
      throw new PaymentError(503, 'Payment service is not available');
    }
  }

  async createPayment(customerId, input) {
    const bookingId = typeof input?.bookingId === 'string' ? input.bookingId : '';
    const method = input?.method;
    let transactionResult;
    let ownershipWasVerified = false;

    try {
      transactionResult = await withTransaction(this.database, async (client) => {
        const booking = await this.paymentRepository.getBookingForUpdate(client, bookingId);
        if (!booking || booking.customer_id !== customerId) {
          throw new PaymentError(404, 'Booking not found');
        }
        if (booking.status !== 'CONFIRMED') {
          throw new PaymentError(409, 'Only a confirmed booking can be paid');
        }
        ownershipWasVerified = true;

        const existing = await this.paymentRepository.getSuccessfulPaymentForBooking(client, bookingId);
        if (existing) return { payment: existing, alreadySaved: true };

        if (!supportedMethods.has(method)) {
          throw new PaymentError(400, 'method must be Card, UPI or Cash');
        }

        const days = Number(booking.days);
        const dailyRate = Number(booking.daily_rate);
        if (!Number.isInteger(days) || days < 1 || !Number.isFinite(dailyRate) || dailyRate <= 0) {
          throw new PaymentError(400, 'Booking dates or vehicle rate are invalid');
        }

        const subtotal = days * dailyRate;
        const discount = 0;
        const tax = (subtotal - discount) * this.taxPercent / 100;
        const amount = subtotal - discount + tax;
        if (!Number.isFinite(amount) || amount <= 0) {
          throw new PaymentError(400, 'Invalid payment amount');
        }

        const payment = {
          paymentId: `PAY-${Date.now()}-${randomBytes(8).toString('hex')}`,
          bookingId,
          amount,
          method,
          status: 'Successful',
        };
        // All supported C++ payment strategies succeed when the calculated amount is positive.
        await this.paymentRepository.insertPayment(client, payment);
        return { payment, alreadySaved: false, subtotal };
      });
    } catch (error) {
      if (error instanceof PaymentError) throw error;
      if (!ownershipWasVerified) {
        await this.recordAudit('UNKNOWN', 'PAYMENT_SAVE_FAILED',
          `Payment for booking ${bookingId} could not be saved`);
        throw paymentUnavailable();
      }

      // A connection failure may occur after PostgreSQL committed. Re-read to recover idempotently.
      try {
        const saved = await this.paymentRepository.getSuccessfulPaymentForBooking(null, bookingId);
        if (saved) transactionResult = { payment: saved, alreadySaved: true };
        else {
          await this.recordAudit('UNKNOWN', 'PAYMENT_SAVE_FAILED',
            `Payment for booking ${bookingId} could not be saved`);
          throw paymentUnavailable();
        }
      } catch (recoveryError) {
        if (recoveryError instanceof PaymentError) throw recoveryError;
        await this.recordAudit('UNKNOWN', 'PAYMENT_SAVE_FAILED',
          `Payment for booking ${bookingId} could not be saved`);
        throw paymentUnavailable();
      }
    }

    const { payment, alreadySaved } = transactionResult;
    if (!alreadySaved) {
      await this.recordAudit(customerId, 'PAYMENT_SAVED',
        `Payment ${payment.paymentId} for booking ${bookingId} recorded with amount ${payment.amount}, method ${payment.method}, status ${payment.status}`);
    }

    try {
      const invoiceResult = await this.invoiceService.createOrGetInvoice({
        bookingId,
        subtotal: alreadySaved ? payment.amount : transactionResult.subtotal,
        taxPercent: alreadySaved ? 0 : this.taxPercent,
      });
      let delivery = {
        status: invoiceResult.invoice.emailStatus,
        message: emailMessage(invoiceResult.invoice.emailStatus),
      };
      if (invoiceResult.created || invoiceResult.invoice.emailStatus === 'PENDING') {
        try {
          delivery = await this.invoiceDeliveryService.sendInvoice(invoiceResult.invoice.invoiceId);
        } catch {
          // Delivery failures never reverse a committed payment or invoice.
          delivery = {
            status: 'SENDING',
            message: 'Invoice is saved; delivery status could not be updated. Check the admin invoice list before retrying.',
          };
        }
      }
      return {
        httpStatus: alreadySaved ? 200 : 201,
        body: {
          paymentId: payment.paymentId,
          invoiceId: invoiceResult.invoice.invoiceId,
          bookingId,
          amount: payment.amount,
          status: payment.status,
          simulation: true,
          emailStatus: delivery.status,
          emailMessage: delivery.message,
        },
      };
    } catch {
      throw paymentUnavailable();
    }
  }
}
