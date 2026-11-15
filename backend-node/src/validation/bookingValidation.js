export class BookingValidationError extends Error {
  constructor(message) {
    super(message);
    this.status = 400;
  }
}

function fail(message) {
  throw new BookingValidationError(message);
}

export function parseBookingDate(value) {
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value)) {
    fail('Booking dates must use YYYY-MM-DD format');
  }

  const year = Number(value.slice(0, 4));
  const month = Number(value.slice(5, 7));
  const day = Number(value.slice(8, 10));
  if (year === 0) {
    fail('Booking date year must be between 0001 and 9999');
  }

  const leapYear = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const daysInMonth = [31, leapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  if (month < 1 || month > 12 || day < 1 || day > daysInMonth[month - 1]) {
    fail('Booking date is not a valid calendar date');
  }

  const priorYear = year - 1;
  const daysBeforeYear = priorYear * 365 + Math.floor(priorYear / 4)
    - Math.floor(priorYear / 100) + Math.floor(priorYear / 400);
  let dayOfYear = day;
  for (let index = 0; index < month - 1; index += 1) {
    dayOfYear += daysInMonth[index];
  }
  return daysBeforeYear + dayOfYear;
}

export function validateBooking({ bookingId, customerId, vehicleId, startDate, endDate }) {
  if (![bookingId, customerId, vehicleId].every((value) =>
    typeof value === 'string' && value.length > 0)) {
    fail('Booking details cannot be empty');
  }

  const start = parseBookingDate(startDate);
  const end = parseBookingDate(endDate);
  if (end < start) {
    fail('End date cannot be earlier than start date');
  }
}
