
#ifndef BOOKING_H
#define BOOKING_H

#include <string>

namespace vrms {

enum class BookingStatus {
    Pending,
    Confirmed,
    Cancelled
};

class Booking {
private:
    std::string bookingId;
    std::string customerId;
    std::string vehicleId;
    std::string startDate;
    std::string endDate;
    BookingStatus status;

public:
    Booking(const std::string& id,
            const std::string& customer,
            const std::string& vehicle,
            const std::string& start,
            const std::string& end);

    std::string getBookingId() const;
    std::string getCustomerId() const;
    std::string getVehicleId() const;
    std::string getStartDate() const;
    std::string getEndDate() const;
    BookingStatus getStatus() const;

    void confirmBooking();
    void cancelBooking();

    bool overlapsWith(const Booking& other) const;
    std::string getStatusString() const;
};

}

#endif
