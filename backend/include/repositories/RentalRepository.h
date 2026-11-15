
#ifndef RENTAL_REPOSITORY_H
#define RENTAL_REPOSITORY_H

#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace vrms {

struct RentalRecord {
    std::string rentalId;
    std::string bookingId;
    std::string plannedReturnDate;
    std::string actualReturnDate;
    double dailyLateFee;
    bool returned;
};

class RentalRepository {
private:
    pqxx::connection connection;

public:
    RentalRepository();

    void createRental(
        const std::string& rentalId,
        const std::string& bookingId,
        const std::string& plannedReturnDate,
        double dailyLateFee
    );

    void completeReturn(
        const std::string& rentalId,
        const std::string& actualReturnDate
    );

    std::vector<RentalRecord> getAllRentals();

    RentalRecord getRentalById(
        const std::string& rentalId
    );
};

}

#endif
