import { withTransaction } from './transaction.js';
import { validateBooking } from '../validation/bookingValidation.js';

function repositoryError(code, message, status) {
  return Object.assign(new Error(message), { code, status });
}

function bookingFromRow(row) {
  return {
    bookingId: row.booking_id,
    customerId: row.customer_id,
    vehicleId: row.vehicle_id,
    startDate: row.start_date,
    endDate: row.end_date,
    status: row.status,
  };
}

export class BookingRepository {
  constructor(database) {
    this.database = database;
  }

  async createBooking(booking) {
    validateBooking(booking);

    return withTransaction(this.database, async (client) => {
      const vehicle = await client.query(
        'SELECT vehicle_id FROM vehicles WHERE vehicle_id = $1 FOR UPDATE',
        [booking.vehicleId],
      );
      if (vehicle.rows.length === 0) {
        throw repositoryError('VEHICLE_NOT_FOUND', 'Vehicle not found', 404);
      }

      const overlap = await client.query(
        'SELECT booking_id FROM bookings '
          + 'WHERE vehicle_id = $1 '
          + "AND status IS DISTINCT FROM 'CANCELLED' "
          + 'AND start_date <= $3::date '
          + 'AND end_date >= $2::date '
          + 'AND booking_id <> $4 LIMIT 1',
        [booking.vehicleId, booking.startDate, booking.endDate, booking.bookingId],
      );
      if (overlap.rows.length > 0) {
        throw repositoryError(
          'BOOKING_OVERLAP',
          'Vehicle is already booked for the requested dates',
          409,
        );
      }

      await client.query(
        'INSERT INTO bookings '
          + '(booking_id, customer_id, vehicle_id, start_date, end_date, status) '
          + "VALUES ($1, $2, $3, $4::date, $5::date, 'CONFIRMED')",
        [booking.bookingId, booking.customerId, booking.vehicleId,
          booking.startDate, booking.endDate],
      );

      return { ...booking, status: 'CONFIRMED' };
    });
  }

  async getCustomerBookings(customerId) {
    const result = await this.database.query(
      'SELECT booking_id, customer_id, vehicle_id, '
        + "to_char(start_date, 'YYYY-MM-DD') AS start_date, "
        + "to_char(end_date, 'YYYY-MM-DD') AS end_date, status "
        + 'FROM bookings WHERE customer_id = $1 ORDER BY start_date',
      [customerId],
    );
    return result.rows.map(bookingFromRow);
  }

  async getAllBookings() {
    const result = await this.database.query(
      'SELECT booking_id, customer_id, vehicle_id, '
        + "to_char(start_date, 'YYYY-MM-DD') AS start_date, "
        + "to_char(end_date, 'YYYY-MM-DD') AS end_date, status "
        + 'FROM bookings ORDER BY start_date DESC, booking_id',
    );
    return result.rows.map(bookingFromRow);
  }

  async cancelBooking(bookingId, customerId = null) {
    const result = customerId === null
      ? await this.database.query(
        "UPDATE bookings SET status = 'CANCELLED' WHERE booking_id = $1 "
          + 'RETURNING booking_id, customer_id',
        [bookingId],
      )
      : await this.database.query(
        "UPDATE bookings SET status = 'CANCELLED' WHERE booking_id = $1 "
          + 'AND customer_id = $2 RETURNING booking_id, customer_id',
        [bookingId, customerId],
      );

    return result.rows[0] ?? null;
  }
}
