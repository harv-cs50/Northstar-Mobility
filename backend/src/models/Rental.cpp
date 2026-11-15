
#include "models/Rental.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace vrms {

namespace {

std::chrono::sys_days parseDate(const std::string& date) {
    int year, month, day;
    char extra;

    if (date.size() != 10 ||
        std::sscanf(date.c_str(), "%4d-%2d-%2d%c",
                    &year, &month, &day, &extra) != 3) {
        throw std::invalid_argument("Date must be in YYYY-MM-DD format");
    }

    std::chrono::year_month_day parsed{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };

    if (!parsed.ok())
        throw std::invalid_argument("Invalid calendar date");

    return std::chrono::sys_days{parsed};
}

}

Rental::Rental(const std::string& id,
               const std::string& booking,
               const std::string& plannedReturn,
               double lateFee)
    : rentalId(id),
      bookingId(booking),
      plannedReturnDate(plannedReturn),
      dailyLateFee(lateFee),
      returned(false) {

    if (id.empty() || booking.empty() || lateFee < 0)
        throw std::invalid_argument("Invalid rental details");

    parseDate(plannedReturn);
}

std::string Rental::getRentalId() const {
    return rentalId;
}

std::string Rental::getBookingId() const {
    return bookingId;
}

bool Rental::isReturned() const {
    return returned;
}

void Rental::completeReturn(const std::string& actualDate) {
    if (returned)
        throw std::logic_error("Vehicle has already been returned");

    auto actual = parseDate(actualDate);
    auto planned = parseDate(plannedReturnDate);

    if (actual < planned)
        throw std::invalid_argument("Invalid return date");

    actualReturnDate = actualDate;
    returned = true;
}

int Rental::calculateLateDays() const {
    if (!returned)
        throw std::logic_error("Vehicle has not been returned");

    auto actual = parseDate(actualReturnDate);
    auto planned = parseDate(plannedReturnDate);

    return static_cast<int>((actual - planned).count());
}

double Rental::calculateLateFee() const {
    return calculateLateDays() * dailyLateFee;
}

}
