import { generateInvoicePdf } from './invoicePdfGenerator.js';

export class InvoiceDeliveryError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

export class InvoiceDeliveryService {
  constructor({ repository, emailService, pdfGenerator = generateInvoicePdf }) {
    this.repository = repository;
    this.emailService = emailService;
    this.pdfGenerator = pdfGenerator;
  }

  async sendInvoice(invoiceId) {
    let claimed;
    try {
      claimed = await this.repository.claimInvoiceEmail(invoiceId);
    } catch {
      return {
        status: 'SENDING',
        message: 'Invoice is saved; delivery status could not be updated. Check the admin invoice list before retrying.',
        sent: false,
      };
    }

    if (!claimed) {
      let status;
      try {
        status = await this.repository.getInvoiceEmailStatus(invoiceId);
      } catch {
        return {
          status: 'SENDING',
          message: 'Invoice is saved; delivery status could not be updated. Check the admin invoice list before retrying.',
          sent: false,
        };
      }
      if (!status) throw new InvoiceDeliveryError(404, 'Invoice not found');
      return {
        status,
        message: 'Invoice email is already sent or another delivery attempt is active.',
        sent: false,
      };
    }

    try {
      const invoice = await this.repository.loadInvoiceDocument(invoiceId);
      if (!invoice) throw new Error('Invoice document not found');
      const pdf = await this.pdfGenerator(invoice);
      await this.emailService.sendInvoice(invoice.customerEmail, invoice.invoiceId, pdf);
    } catch {
      try {
        await this.repository.markInvoiceEmailFailed(invoiceId);
        return {
          status: 'FAILED',
          message: 'Payment and invoice are saved, but email delivery failed. An administrator can retry it.',
          sent: false,
        };
      } catch {
        return {
          status: 'SENDING',
          message: 'Invoice is saved; delivery status could not be updated. Check the admin invoice list before retrying.',
          sent: false,
        };
      }
    }

    try {
      await this.repository.markInvoiceEmailSent(invoiceId);
    } catch {
      // The email provider accepted the message. Keep SENDING so a retry cannot send it twice.
      return {
        status: 'SENDING',
        message: 'The email server accepted the invoice, but delivery status could not be updated. Do not resend until status is checked.',
        sent: false,
      };
    }

    return {
      status: 'SENT',
      message: "Invoice PDF emailed to the customer's registered email address.",
      sent: true,
    };
  }
}
