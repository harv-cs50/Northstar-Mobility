
#ifndef BOOKING_REPOSITORY_H
#define BOOKING_REPOSITORY_H

#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace vrms {

struct BookingRecord {
    std::string bookingId;
    std::string customerId;
    std::string vehicleId;
    std::string startDate;
    std::string endDate;
    std::string status;
};

class BookingRepository {
private:
    pqxx::connection connection;

public:
    BookingRepository();

    void createBooking(
        const std::string& bookingId,
        const std::string& customerId,
        const std::string& vehicleId,
        const std::string& startDate,
        const std::string& endDate
    );

    void cancelBooking(const std::string& bookingId);

    std::vector<BookingRecord> getCustomerBookings(
        const std::string& customerId
    );

    std::vector<BookingRecord> getAllBookings();
};

}

#endif
