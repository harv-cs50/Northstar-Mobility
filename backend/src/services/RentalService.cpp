
#include "services/RentalService.h"
#include "models/Rental.h"

#include <stdexcept>

namespace vrms {

void RentalService::createRental(
    const std::string& rentalId,
    const std::string& bookingId,
    const std::string& plannedReturnDate,
    double dailyLateFee
) {
    Rental rental(
        rentalId,
        bookingId,
        plannedReturnDate,
        dailyLateFee
    );

    repository.createRental(
        rentalId,
        bookingId,
        plannedReturnDate,
        dailyLateFee
    );
}

double RentalService::completeReturn(
    const std::string& rentalId,
    const std::string& actualReturnDate
) {
    RentalRecord record = repository.getRentalById(rentalId);

    if (record.returned) {
        throw std::runtime_error("Vehicle already returned");
    }

    Rental rental(
        record.rentalId,
        record.bookingId,
        record.plannedReturnDate,
        record.dailyLateFee
    );

    rental.completeReturn(actualReturnDate);

    double lateFee = rental.calculateLateFee();

    repository.completeReturn(
        rentalId,
        actualReturnDate
    );

    return lateFee;
}

std::vector<RentalRecord> RentalService::getAllRentals() {
    return repository.getAllRentals();
}

RentalRecord RentalService::getRentalById(
    const std::string& rentalId
) {
    return repository.getRentalById(rentalId);
}

}
