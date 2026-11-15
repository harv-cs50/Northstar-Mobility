
#include "repositories/RentalRepository.h"
#include "DatabaseConfig.h"
#include "repositories/AuditLogRepository.h"
#include "models/AuditLog.h"

#include <stdexcept>
#include <iostream>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace {

std::string generateRentalAuditId() {
    auto now = std::chrono::system_clock::now();

    auto milliseconds = std::chrono::duration_cast<
        std::chrono::milliseconds
    >(now.time_since_epoch()).count();

    static std::mt19937 generator(std::random_device{}());

    std::uniform_int_distribution<int> distribution(0, 9999);

    std::ostringstream id;

    id << "AUD"
       << milliseconds
       << std::setw(4)
       << std::setfill('0')
       << distribution(generator);

    return id.str();
}

void writeRentalAudit(
    const std::string& userId,
    const std::string& action,
    const std::string& description
) noexcept {
    try {
        vrms::AuditLogRepository repository;

        vrms::AuditLog log(
            generateRentalAuditId(),
            userId.empty() ? "UNKNOWN" : userId,
            action,
            description
        );

        repository.saveLog(log);

        std::cout << "[AUDIT] "
                  << action
                  << " recorded successfully.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[AUDIT ERROR] "
                  << e.what() << "\n";
    }
    catch (...) {
        std::cerr << "[AUDIT ERROR] Unknown error occurred.\n";
    }
}

}

namespace vrms {

RentalRepository::RentalRepository()
    : connection(createDatabaseConnection()) {}

void RentalRepository::createRental(
    const std::string& rentalId,
    const std::string& bookingId,
    const std::string& plannedReturnDate,
    double dailyLateFee
) {
    if (
        rentalId.empty() ||
        bookingId.empty() ||
        plannedReturnDate.empty() ||
        dailyLateFee < 0
    ) {
        writeRentalAudit(
            "UNKNOWN",
            "RENTAL_CREATE_FAILED",
            "Rental " + rentalId +
            " rejected due to invalid rental details"
        );

        throw std::invalid_argument("Invalid rental details");
    }

    std::string customerId = "UNKNOWN";

    try {
        pqxx::work transaction(connection);

        pqxx::result bookingResult = transaction.exec(
            "SELECT customer_id FROM bookings WHERE booking_id = $1",
            pqxx::params{bookingId}
        );

        if (!bookingResult.empty()) {
            customerId =
                bookingResult[0]["customer_id"].as<std::string>();
        }

        transaction.exec(
            "INSERT INTO rentals "
            "(rental_id, booking_id, planned_return_date, daily_late_fee) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (rental_id) DO UPDATE SET "
            "booking_id = EXCLUDED.booking_id, "
            "planned_return_date = EXCLUDED.planned_return_date, "
            "daily_late_fee = EXCLUDED.daily_late_fee, "
            "actual_return_date = NULL",
            pqxx::params{
                rentalId,
                bookingId,
                plannedReturnDate,
                dailyLateFee
            }
        );

        transaction.commit();

        writeRentalAudit(
            customerId,
            "RENTAL_CREATED",
            "Rental " + rentalId +
            " created for booking " + bookingId +
            ". Planned return date: " + plannedReturnDate
        );
    }
    catch (const std::exception&) {
        writeRentalAudit(
            customerId,
            "RENTAL_CREATE_FAILED",
            "Rental " + rentalId +
            " for booking " + bookingId +
            " could not be created"
        );

        throw;
    }
}

void RentalRepository::completeReturn(
    const std::string& rentalId,
    const std::string& actualReturnDate
) {
    if (rentalId.empty() || actualReturnDate.empty()) {
        writeRentalAudit(
            "UNKNOWN",
            "RENTAL_RETURN_FAILED",
            "Return rejected due to invalid rental ID or return date"
        );

        throw std::invalid_argument("Invalid return details");
    }

    std::string customerId = "UNKNOWN";

    try {
        pqxx::work transaction(connection);

        pqxx::result rentalDetails = transaction.exec(
            "SELECT b.customer_id "
            "FROM rentals r "
            "JOIN bookings b ON b.booking_id = r.booking_id "
            "WHERE r.rental_id = $1",
            pqxx::params{rentalId}
        );

        if (rentalDetails.empty()) {
            throw std::runtime_error("Rental not found");
        }

        customerId =
            rentalDetails[0]["customer_id"].as<std::string>();

        pqxx::result result = transaction.exec(
            "UPDATE rentals "
            "SET actual_return_date = $1 "
            "WHERE rental_id = $2 "
            "AND actual_return_date IS NULL",
            pqxx::params{
                actualReturnDate,
                rentalId
            }
        );

        if (result.affected_rows() == 0) {
            throw std::runtime_error("Vehicle already returned");
        }

        transaction.commit();

        writeRentalAudit(
            customerId,
            "RENTAL_RETURNED",
            "Rental " + rentalId +
            " returned successfully on " + actualReturnDate
        );
    }
    catch (const std::exception& e) {
        writeRentalAudit(
            customerId,
            "RENTAL_RETURN_FAILED",
            "Return of rental " + rentalId +
            " failed: " + e.what()
        );

        throw;
    }
}

std::vector<RentalRecord> RentalRepository::getAllRentals() {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT rental_id, booking_id, planned_return_date, "
        "actual_return_date, daily_late_fee "
        "FROM rentals ORDER BY rental_id"
    );

    std::vector<RentalRecord> rentals;

    for (const auto& row : result) {
        rentals.push_back({
            row["rental_id"].as<std::string>(),
            row["booking_id"].as<std::string>(),
            row["planned_return_date"].as<std::string>(),
            row["actual_return_date"].is_null()
                ? ""
                : row["actual_return_date"].as<std::string>(),
            row["daily_late_fee"].as<double>(),
            !row["actual_return_date"].is_null()
        });
    }

    transaction.commit();

    return rentals;
}

RentalRecord RentalRepository::getRentalById(
    const std::string& rentalId
) {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT rental_id, booking_id, planned_return_date, "
        "actual_return_date, daily_late_fee "
        "FROM rentals WHERE rental_id = $1",
        pqxx::params{rentalId}
    );

    if (result.empty()) {
        throw std::runtime_error("Rental not found");
    }

    const auto& row = result[0];

    RentalRecord rental{
        row["rental_id"].as<std::string>(),
        row["booking_id"].as<std::string>(),
        row["planned_return_date"].as<std::string>(),
        row["actual_return_date"].is_null()
            ? ""
            : row["actual_return_date"].as<std::string>(),
        row["daily_late_fee"].as<double>(),
        !row["actual_return_date"].is_null()
    };

    transaction.commit();

    return rental;
}

}
