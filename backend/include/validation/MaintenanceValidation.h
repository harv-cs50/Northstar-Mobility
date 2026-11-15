#ifndef MAINTENANCE_VALIDATION_H
#define MAINTENANCE_VALIDATION_H

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace vrms::maintenance_validation {

inline constexpr char ScheduledStatus[] = "SCHEDULED";
inline constexpr char CompletedStatus[] = "COMPLETED";

inline bool isValidDate(const std::string& dateString) {
    if (
        dateString.size() != 10 ||
        dateString[4] != '-' ||
        dateString[7] != '-'
    ) {
        return false;
    }

    for (std::size_t i = 0; i < dateString.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }

        if (!std::isdigit(
            static_cast<unsigned char>(dateString[i])
        )) {
            return false;
        }
    }

    try {
        const int yearValue = std::stoi(dateString.substr(0, 4));
        if (yearValue == 0) {
            return false;
        }

        const std::chrono::year_month_day date{
            std::chrono::year{yearValue},
            std::chrono::month{
                static_cast<unsigned>(
                    std::stoi(dateString.substr(5, 2))
                )
            },
            std::chrono::day{
                static_cast<unsigned>(
                    std::stoi(dateString.substr(8, 2))
                )
            }
        };

        return date.ok();
    }
    catch (const std::exception&) {
        return false;
    }
}

inline void validateSchedule(
    const std::string& maintenanceId,
    const std::string& vehicleId,
    const std::string& serviceDate,
    const std::string& description,
    double maintenanceCost
) {
    if (
        maintenanceId.empty() ||
        vehicleId.empty() ||
        serviceDate.empty() ||
        description.empty()
    ) {
        throw std::invalid_argument(
            "Maintenance details cannot be empty"
        );
    }

    if (!std::isfinite(maintenanceCost) || maintenanceCost <= 0) {
        throw std::invalid_argument(
            "Maintenance cost must be greater than zero"
        );
    }

    if (!isValidDate(serviceDate)) {
        throw std::invalid_argument(
            "Service date must be a valid YYYY-MM-DD date"
        );
    }
}

inline void validateCompletionInput(
    const std::string& maintenanceId,
    const std::string& completionDate
) {
    if (maintenanceId.empty() || completionDate.empty()) {
        throw std::invalid_argument(
            "Maintenance ID and completion date are required"
        );
    }

    if (!isValidDate(completionDate)) {
        throw std::invalid_argument(
            "Completion date must be a valid YYYY-MM-DD date"
        );
    }
}

inline void validateStatusForCompletion(const std::string& status) {
    if (status != ScheduledStatus) {
        throw std::runtime_error("Maintenance is already completed");
    }
}

inline void validateCompletionDate(
    const std::string& completionDate,
    const std::string& serviceDate
) {
    if (!isValidDate(serviceDate)) {
        throw std::invalid_argument(
            "Service date must be a valid YYYY-MM-DD date"
        );
    }

    if (completionDate < serviceDate) {
        throw std::invalid_argument(
            "Completion date cannot be earlier than service date"
        );
    }
}

}

#endif
