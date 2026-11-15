
#ifndef RENTAL_SERVICE_H
#define RENTAL_SERVICE_H

#include "repositories/RentalRepository.h"
#include <string>
#include <vector>

namespace vrms {

class RentalService {
private:
    RentalRepository repository;

public:
    void createRental(
        const std::string& rentalId,
        const std::string& bookingId,
        const std::string& plannedReturnDate,
        double dailyLateFee
    );

    double completeReturn(
        const std::string& rentalId,
        const std::string& actualReturnDate
    );

    std::vector<RentalRecord> getAllRentals();

    RentalRecord getRentalById(const std::string& rentalId);
};

}

#endif
