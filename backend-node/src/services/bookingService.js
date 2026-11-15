import { randomBytes } from 'node:crypto';
import { BookingRepository } from '../repositories/bookingRepository.js';
import { BookingValidationError, validateBooking } from '../validation/bookingValidation.js';

export class BookingError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function unavailable() {
  return new BookingError(503, 'Booking service is not available');
}

export class BookingService {
  constructor(repository, audit = null) {
    this.repository = repository;
    this.audit = audit;
  }

  async recordAudit(userId, action, description) {
    try {
      await this.audit?.write(userId, action, description);
    } catch {
      // Preserve the existing C++ behavior: audit failures do not roll back bookings.
    }
  }

  async getBookings(principal) {
    try {
      return principal.role === 'ADMIN'
        ? await this.repository.getAllBookings()
        : await this.repository.getCustomerBookings(principal.userId);
    } catch {
      throw unavailable();
    }
  }

  async createBooking(customerId, input) {
    const booking = {
      bookingId: `BK-${Date.now()}-${randomBytes(8).toString('hex')}`,
      customerId,
      vehicleId: input?.vehicleId,
      startDate: input?.startDate,
      endDate: input?.endDate,
    };

    try {
      validateBooking(booking);
      const created = await this.repository.createBooking(booking);
      await this.recordAudit(customerId, 'BOOKING_CREATED',
        `Booking ${created.bookingId} was created for vehicle ${created.vehicleId} from ${created.startDate} to ${created.endDate}`);
      return created;
    } catch (error) {
      await this.recordAudit(customerId, 'BOOKING_CREATE_FAILED',
        `Booking ${booking.bookingId} could not be created`);
      if (error instanceof BookingValidationError) throw error;
      if (error?.code === 'VEHICLE_NOT_FOUND' || error?.code === 'BOOKING_OVERLAP') {
        throw new BookingError(error.status, error.message);
      }
      if (error?.code === '23505') {
        throw new BookingError(409, 'Booking ID already exists');
      }
      throw unavailable();
    }
  }

  async cancelBooking(bookingId, principal) {
    try {
      const booking = await this.repository.cancelBooking(
        bookingId,
        principal.role === 'ADMIN' ? null : principal.userId,
      );
      if (!booking) throw new BookingError(404, 'Booking not found');
      await this.recordAudit(booking.customer_id, 'BOOKING_CANCELLED',
        `Booking ${bookingId} was cancelled`);
      return { success: true };
    } catch (error) {
      if (error instanceof BookingError) {
        if (error.status === 404) {
          await this.recordAudit('UNKNOWN', 'BOOKING_CANCEL_FAILED',
            `Cancellation requested for booking ${bookingId}, but no matching booking was found`);
        }
        throw error;
      }
      await this.recordAudit('UNKNOWN', 'BOOKING_CANCEL_FAILED',
        `Booking ${bookingId} could not be cancelled`);
      throw unavailable();
    }
  }
}

export function createBookingService(database, audit = null) {
  return new BookingService(new BookingRepository(database), audit);
}
