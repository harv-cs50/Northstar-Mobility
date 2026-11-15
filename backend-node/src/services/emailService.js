import nodemailer from 'nodemailer';
import { isValidEmail, loadSmtpConfig } from './smtpConfig.js';

export class EmailService {
  constructor({ environment = process.env, transportFactory = nodemailer.createTransport } = {}) {
    this.environment = environment;
    this.transportFactory = transportFactory;
  }

  async sendInvoice(recipient, invoiceId, pdfBytes) {
    if (!isValidEmail(recipient)) throw new Error('Invoice recipient email is invalid');
    if (typeof invoiceId !== 'string' || !invoiceId || /[\/\\\r\n]/.test(invoiceId)) {
      throw new Error('Invoice ID is not valid for an email attachment');
    }
    const attachment = Buffer.isBuffer(pdfBytes) ? pdfBytes : Buffer.from(pdfBytes ?? []);
    if (attachment.length < 5 || attachment.subarray(0, 5).toString('ascii') !== '%PDF-') {
      throw new Error('Invoice email attachment is not a valid PDF');
    }

    const config = loadSmtpConfig(this.environment);
    let transport;
    try {
      transport = this.transportFactory({
        host: config.host,
        port: config.port,
        secure: config.security === 'ssl',
        requireTLS: config.security === 'starttls',
        auth: { user: config.username, pass: config.password },
        connectionTimeout: config.timeoutSeconds * 1000,
        greetingTimeout: config.timeoutSeconds * 1000,
        socketTimeout: config.timeoutSeconds * 1000,
        tls: { minVersion: 'TLSv1.2' },
        logger: false,
        debug: false,
      });
      await transport.sendMail({
        from: { name: config.fromName, address: config.fromEmail },
        to: recipient,
        subject: `Your vehicle rental invoice ${invoiceId}`,
        text: `Hello,\n\nYour vehicle rental invoice ${invoiceId} is attached as a PDF.\n\nThis application uses simulated payments; no real payment was processed.\n`,
        attachments: [{
          filename: `invoice-${invoiceId}.pdf`,
          content: attachment,
          contentType: 'application/pdf',
        }],
      });
    } catch {
      // Never forward SMTP library errors; they may contain server or credential details.
      throw new Error('SMTP server did not accept the invoice message');
    } finally {
      try {
        transport?.close?.();
      } catch {
        // Closing a transport does not alter the recorded delivery result.
      }
    }
  }
}
