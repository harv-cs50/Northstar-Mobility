function paymentFromRow(row) {
  return {
    paymentId: row.payment_id,
    bookingId: row.booking_id,
    amount: Number(row.amount),
    method: row.payment_method,
    status: row.payment_status,
  };
}

export class PaymentRepository {
  constructor(database) {
    this.database = database;
  }

  async getBookingForUpdate(client, bookingId) {
    const result = await client.query(
      'SELECT b.customer_id, b.status, '
        + 'GREATEST(b.end_date - b.start_date, 1) AS days, '
        + 'v.daily_rate '
        + 'FROM bookings b JOIN vehicles v USING (vehicle_id) '
        + 'WHERE b.booking_id = $1 FOR UPDATE OF b',
      [bookingId],
    );
    return result.rows[0] ?? null;
  }

  async getSuccessfulPaymentForBooking(client, bookingId) {
    const connection = client ?? this.database;
    const result = await connection.query(
      "SELECT payment_id, booking_id, amount, payment_method, payment_status "
        + "FROM payments WHERE booking_id = $1 AND payment_status = 'Successful' "
        + 'ORDER BY payment_id DESC LIMIT 1',
      [bookingId],
    );
    return result.rows[0] ? paymentFromRow(result.rows[0]) : null;
  }

  async insertPayment(client, payment) {
    await client.query(
      'INSERT INTO payments '
        + '(payment_id, booking_id, amount, payment_method, payment_status) '
        + "VALUES ($1, $2, $3, $4, 'Successful')",
      [payment.paymentId, payment.bookingId, payment.amount, payment.method],
    );
  }

  async getAllPayments() {
    const result = await this.database.query(
      'SELECT payment_id, booking_id, amount, payment_method, payment_status '
        + 'FROM payments ORDER BY payment_id',
    );
    return result.rows.map(paymentFromRow);
  }

  async getCustomerPayments(customerId) {
    const result = await this.database.query(
      'SELECT p.payment_id, p.booking_id, p.amount, p.payment_method, p.payment_status '
        + 'FROM payments p JOIN bookings b ON b.booking_id = p.booking_id '
        + 'WHERE b.customer_id = $1 ORDER BY p.payment_id',
      [customerId],
    );
    return result.rows.map(paymentFromRow);
  }
}
