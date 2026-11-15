
#ifndef RENTAL_H
#define RENTAL_H

#include <string>

namespace vrms {

class Rental {
private:
    std::string rentalId;
    std::string bookingId;
    std::string plannedReturnDate;
    std::string actualReturnDate;
    double dailyLateFee;
    bool returned;

public:
    Rental(const std::string& id,
           const std::string& booking,
           const std::string& plannedReturn,
           double lateFee);

    std::string getRentalId() const;
    std::string getBookingId() const;
    bool isReturned() const;

    void completeReturn(const std::string& actualDate);

    int calculateLateDays() const;
    double calculateLateFee() const;
};

}

#endif
