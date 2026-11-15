import { withTransaction } from './transaction.js';

const invoiceColumns = 'invoice_id, booking_id, subtotal, discount, tax, total_amount, '
  + 'email_status, email_attempts';

function invoiceFromRow(row) {
  return {
    invoiceId: row.invoice_id,
    bookingId: row.booking_id,
    subtotal: Number(row.subtotal),
    discount: Number(row.discount),
    tax: Number(row.tax),
    totalAmount: Number(row.total_amount),
    emailStatus: row.email_status,
    emailAttempts: Number(row.email_attempts),
  };
}

export class InvoiceRepository {
  constructor(database) {
    this.database = database;
  }

  async createOrGetInvoice(invoice) {
    return withTransaction(this.database, async (client) => {
      const booking = await client.query(
        'SELECT customer_id FROM bookings WHERE booking_id = $1 FOR UPDATE',
        [invoice.bookingId],
      );
      if (booking.rows.length === 0) {
        const error = new Error('Booking not found for invoice');
        error.code = 'BOOKING_NOT_FOUND';
        throw error;
      }

      const existing = await client.query(
        `SELECT ${invoiceColumns} FROM invoices WHERE booking_id = $1 ORDER BY invoice_id LIMIT 1`,
        [invoice.bookingId],
      );
      if (existing.rows[0]) {
        return {
          invoice: invoiceFromRow(existing.rows[0]),
          created: false,
          customerId: booking.rows[0].customer_id,
        };
      }

      const inserted = await client.query(
        'INSERT INTO invoices '
          + '(invoice_id, booking_id, subtotal, discount, tax, total_amount, email_status, email_attempts) '
          + "VALUES ($1, $2, $3, $4, $5, $6, 'PENDING', 0) "
          + 'ON CONFLICT (booking_id) DO NOTHING '
          + `RETURNING ${invoiceColumns}`,
        [invoice.invoiceId, invoice.bookingId, invoice.subtotal,
          invoice.discount, invoice.tax, invoice.totalAmount],
      );

      let row = inserted.rows[0];
      let created = Boolean(row);
      if (!row) {
        const recovered = await client.query(
          `SELECT ${invoiceColumns} FROM invoices WHERE booking_id = $1 ORDER BY invoice_id LIMIT 1`,
          [invoice.bookingId],
        );
        row = recovered.rows[0];
      }
      if (!row) throw new Error('Invoice could not be created or recovered');

      return {
        invoice: invoiceFromRow(row),
        created,
        customerId: booking.rows[0].customer_id,
      };
    });
  }

  async getAllInvoices() {
    const result = await this.database.query(
      `SELECT ${invoiceColumns} FROM invoices ORDER BY invoice_id`,
    );
    return result.rows.map(invoiceFromRow);
  }

  async getCustomerInvoices(customerId) {
    const result = await this.database.query(
      'SELECT i.invoice_id, i.booking_id, i.subtotal, i.discount, i.tax, '
        + 'i.total_amount, i.email_status, i.email_attempts '
        + 'FROM invoices i JOIN bookings b ON b.booking_id = i.booking_id '
        + 'WHERE b.customer_id = $1 ORDER BY i.invoice_id',
      [customerId],
    );
    return result.rows.map(invoiceFromRow);
  }

  async loadInvoiceDocument(invoiceId) {
    const result = await this.database.query(
      'SELECT i.invoice_id, i.booking_id, i.subtotal, i.discount, i.tax, i.total_amount, '
        + 'b.customer_id, u.name AS customer_name, u.email AS customer_email, '
        + 'v.vehicle_id, v.brand, v.model, v.vehicle_type, v.daily_rate, '
        + "to_char(b.start_date, 'YYYY-MM-DD') AS start_date, "
        + "to_char(b.end_date, 'YYYY-MM-DD') AS end_date, "
        + 'GREATEST(b.end_date - b.start_date, 1) AS duration_days, '
        + "COALESCE(p.payment_status, 'Unknown') AS payment_status "
        + 'FROM invoices i '
        + 'JOIN bookings b ON b.booking_id = i.booking_id '
        + 'JOIN users u ON u.user_id = b.customer_id '
        + 'JOIN vehicles v ON v.vehicle_id = b.vehicle_id '
        + 'LEFT JOIN LATERAL ('
        + ' SELECT payment_status FROM payments WHERE booking_id = b.booking_id '
        + ' ORDER BY payment_id DESC LIMIT 1'
        + ') p ON TRUE '
        + 'WHERE i.invoice_id = $1',
      [invoiceId],
    );
    const row = result.rows[0];
    if (!row) return null;
    return {
      invoiceId: row.invoice_id,
      bookingId: row.booking_id,
      customerId: row.customer_id,
      customerName: row.customer_name,
      customerEmail: row.customer_email,
      vehicleId: row.vehicle_id,
      vehicleBrand: row.brand,
      vehicleModel: row.model,
      vehicleType: row.vehicle_type,
      rentalStartDate: row.start_date,
      rentalEndDate: row.end_date,
      durationDays: Number(row.duration_days),
      dailyRate: Number(row.daily_rate),
      subtotal: Number(row.subtotal),
      discount: Number(row.discount),
      tax: Number(row.tax),
      total: Number(row.total_amount),
      paymentStatus: row.payment_status,
    };
  }

  async claimInvoiceEmail(invoiceId) {
    const result = await this.database.query(
      "UPDATE invoices SET email_status = 'SENDING', "
        + 'email_attempts = email_attempts + 1, '
        + 'email_last_attempt_at = CURRENT_TIMESTAMP, email_last_error = NULL '
        + "WHERE invoice_id = $1 AND email_status IN ('PENDING', 'FAILED') "
        + 'RETURNING invoice_id',
      [invoiceId],
    );
    return result.rowCount === 1;
  }

  async markInvoiceEmailSent(invoiceId) {
    const result = await this.database.query(
      "UPDATE invoices SET email_status = 'SENT', email_sent_at = CURRENT_TIMESTAMP, "
        + "email_last_error = NULL WHERE invoice_id = $1 AND email_status = 'SENDING' "
        + 'RETURNING invoice_id',
      [invoiceId],
    );
    if (result.rowCount !== 1) {
      throw new Error('Invoice email delivery state could not be marked sent');
    }
  }

  async markInvoiceEmailFailed(invoiceId) {
    const result = await this.database.query(
      "UPDATE invoices SET email_status = 'FAILED', email_last_error = $1 "
        + "WHERE invoice_id = $2 AND email_status = 'SENDING' RETURNING invoice_id",
      ['Email provider delivery failed; check server configuration', invoiceId],
    );
    if (result.rowCount !== 1) {
      throw new Error('Invoice email delivery state could not be marked failed');
    }
  }

  async getInvoiceEmailStatus(invoiceId) {
    const result = await this.database.query(
      'SELECT email_status FROM invoices WHERE invoice_id = $1',
      [invoiceId],
    );
    return result.rows[0]?.email_status ?? null;
  }
}
