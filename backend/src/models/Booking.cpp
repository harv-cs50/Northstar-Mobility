
#include "models/Booking.h"
#include "validation/BookingValidation.h"
#include <stdexcept>

namespace vrms {

Booking::Booking(const std::string& id,
                 const std::string& customer,
                 const std::string& vehicle,
                 const std::string& start,
                 const std::string& end)
    : bookingId(id),
      customerId(customer),
      vehicleId(vehicle),
      startDate(start),
      endDate(end),
      status(BookingStatus::Pending) {

    booking_validation::validateBooking(
        id,
        customer,
        vehicle,
        start,
        end
    );
}

std::string Booking::getBookingId() const {
    return bookingId;
}

std::string Booking::getCustomerId() const {
    return customerId;
}

std::string Booking::getVehicleId() const {
    return vehicleId;
}

std::string Booking::getStartDate() const {
    return startDate;
}

std::string Booking::getEndDate() const {
    return endDate;
}

BookingStatus Booking::getStatus() const {
    return status;
}

void Booking::confirmBooking() {
    if (status != BookingStatus::Pending)
        throw std::logic_error("Only pending bookings can be confirmed");

    status = BookingStatus::Confirmed;
}

void Booking::cancelBooking() {
    if (status == BookingStatus::Cancelled)
        throw std::logic_error("Booking is already cancelled");

    status = BookingStatus::Cancelled;
}

bool Booking::overlapsWith(const Booking& other) const {
    if (vehicleId != other.vehicleId)
        return false;

    if (status == BookingStatus::Cancelled ||
        other.status == BookingStatus::Cancelled)
        return false;

    return booking_validation::parseDate(startDate) <=
               booking_validation::parseDate(other.endDate) &&
           booking_validation::parseDate(other.startDate) <=
               booking_validation::parseDate(endDate);
}

std::string Booking::getStatusString() const {
    switch (status) {
        case BookingStatus::Pending: return "Pending";
        case BookingStatus::Confirmed: return "Confirmed";
        case BookingStatus::Cancelled: return "Cancelled";
    }

    return "Unknown";
}

}
