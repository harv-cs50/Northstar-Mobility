import { randomBytes } from 'node:crypto';
import { InvoiceRepository } from '../repositories/invoiceRepository.js';

export class InvoiceService {
  constructor(database, { repository = new InvoiceRepository(database), audit = null } = {}) {
    this.repository = repository;
    this.audit = audit;
  }

  async recordAudit(userId, action, description) {
    try {
      await this.audit?.write(userId, action, description);
    } catch {
      // Match the C++ behavior: audit storage errors do not undo invoice persistence.
    }
  }

  async createOrGetInvoice({ bookingId, invoiceId, subtotal, taxPercent, discountPercent = 0 }) {
    const discount = subtotal * discountPercent / 100;
    const tax = (subtotal - discount) * taxPercent / 100;
    const totalAmount = subtotal - discount + tax;
    const invoice = {
      invoiceId: invoiceId ?? `INV-${Date.now()}-${randomBytes(8).toString('hex')}`,
      bookingId,
      // The C++ repository reverses its total calculation to persist the base subtotal.
      subtotal: totalAmount + discount - tax,
      discount,
      tax,
      totalAmount,
    };

    try {
      const result = await this.repository.createOrGetInvoice(invoice);
      if (result.created) {
        await this.recordAudit(result.customerId, 'INVOICE_SAVED',
          `Invoice ${result.invoice.invoiceId} created for booking ${bookingId} with total amount ${result.invoice.totalAmount}`);
      }
      return result;
    } catch (error) {
      await this.recordAudit('UNKNOWN', 'INVOICE_SAVE_FAILED',
        `Invoice for booking ${bookingId} could not be created`);
      throw error;
    }
  }

  async getInvoices(principal) {
    return principal.role === 'ADMIN'
      ? this.repository.getAllInvoices()
      : this.repository.getCustomerInvoices(principal.userId);
  }
}
