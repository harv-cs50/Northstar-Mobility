
import { Resend } from 'resend';
import { isValidEmail } from './emailValidation.js';

export class EmailService {
  constructor({
    environment = process.env,
    resendFactory = (apiKey) => new Resend(apiKey),
  } = {}) {
    this.environment = environment;
    this.resendFactory = resendFactory;
  }

  async sendInvoice(recipient, invoiceId, pdfBytes) {
    if (!isValidEmail(recipient)) {
      throw new Error('Invoice recipient email is invalid');
    }

    if (
      typeof invoiceId !== 'string' ||
      !invoiceId ||
      /[\/\\\r\n]/.test(invoiceId)
    ) {
      throw new Error('Invoice ID is not valid for an email attachment');
    }

    const attachment = Buffer.isBuffer(pdfBytes)
      ? pdfBytes
      : Buffer.from(pdfBytes ?? []);

    if (
      attachment.length < 5 ||
      attachment.subarray(0, 5).toString('ascii') !== '%PDF-'
    ) {
      throw new Error('Invoice email attachment is not a valid PDF');
    }

    const apiKey = this.environment.RESEND_API_KEY;
    const fromEmail = this.environment.RESEND_FROM_EMAIL;
    const fromName = this.environment.RESEND_FROM_NAME || 'Northstar Mobility';

    if (typeof apiKey !== 'string' || !apiKey.trim()) {
      throw new Error('Resend API key is not configured');
    }

    if (!isValidEmail(fromEmail)) {
      throw new Error('Resend sender email is invalid');
    }

    if (
      typeof fromName !== 'string' ||
      !fromName.trim() ||
      /[\r\n]/.test(fromName)
    ) {
      throw new Error('Resend sender name is invalid');
    }

    try {
      const resend = this.resendFactory(apiKey);

      const { error } = await resend.emails.send({
        from: `${fromName} <${fromEmail}>`,
        to: [recipient],
        subject: `Your vehicle rental invoice ${invoiceId}`,
        text:
          `Hello,\n\n` +
          `Your vehicle rental invoice ${invoiceId} is attached as a PDF.\n\n` +
          `This application uses simulated payments; no real payment was processed.\n`,
        attachments: [
          {
            filename: `invoice-${invoiceId}.pdf`,
            content: attachment,
            contentType: 'application/pdf',
          },
        ],
      });

      if (error) {
        throw new Error('Resend rejected the invoice message');
      }
    } catch {
      throw new Error('Resend server did not accept the invoice message');
    }
  }
}
