#ifndef BOOKING_VALIDATION_H
#define BOOKING_VALIDATION_H

#include <chrono>
#include <cctype>
#include <stdexcept>
#include <string>

namespace vrms::booking_validation {

inline std::chrono::sys_days parseDate(const std::string& value) {
    if (
        value.size() != 10 ||
        value[4] != '-' ||
        value[7] != '-'
    ) {
        throw std::invalid_argument(
            "Booking dates must use YYYY-MM-DD format"
        );
    }

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) {
            continue;
        }

        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            throw std::invalid_argument(
                "Booking dates must use YYYY-MM-DD format"
            );
        }
    }

    const int yearValue =
        (value[0] - '0') * 1000 +
        (value[1] - '0') * 100 +
        (value[2] - '0') * 10 +
        (value[3] - '0');
    const unsigned monthValue =
        static_cast<unsigned>((value[5] - '0') * 10 + (value[6] - '0'));
    const unsigned dayValue =
        static_cast<unsigned>((value[8] - '0') * 10 + (value[9] - '0'));

    if (yearValue == 0) {
        throw std::invalid_argument(
            "Booking date year must be between 0001 and 9999"
        );
    }

    const std::chrono::year_month_day parsedDate{
        std::chrono::year{yearValue},
        std::chrono::month{monthValue},
        std::chrono::day{dayValue}
    };

    if (!parsedDate.ok()) {
        throw std::invalid_argument(
            "Booking date is not a valid calendar date"
        );
    }

    return std::chrono::sys_days{parsedDate};
}

inline void validateBooking(
    const std::string& bookingId,
    const std::string& customerId,
    const std::string& vehicleId,
    const std::string& startDate,
    const std::string& endDate
) {
    if (
        bookingId.empty() ||
        customerId.empty() ||
        vehicleId.empty()
    ) {
        throw std::invalid_argument(
            "Booking details cannot be empty"
        );
    }

    const auto start = parseDate(startDate);
    const auto end = parseDate(endDate);

    if (end < start) {
        throw std::invalid_argument(
            "End date cannot be earlier than start date"
        );
    }
}

}

#endif
