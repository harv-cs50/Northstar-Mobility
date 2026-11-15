
#include "repositories/BookingRepository.h"
#include "DatabaseConfig.h"
#include "validation/BookingValidation.h"
#include <stdexcept>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include "repositories/AuditLogRepository.h"
#include "models/AuditLog.h"


namespace {

std::string generateBookingAuditId() {
    auto now = std::chrono::system_clock::now();
    auto milliseconds = std::chrono::duration_cast<
        std::chrono::milliseconds
    >(now.time_since_epoch()).count();

    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 9999);

    std::ostringstream id;
    id << "AUD" << milliseconds
       << std::setw(4) << std::setfill('0')
       << distribution(generator);

    return id.str();
}

void writeBookingAudit(
    const std::string& userId,
    const std::string& action,
    const std::string& description
) noexcept {
    try {
        vrms::AuditLogRepository repository;
        vrms::AuditLog log(
            generateBookingAuditId(),
            userId.empty() ? "UNKNOWN" : userId,
            action,
            description
        );
        repository.saveLog(log);
    } catch (...) {
        // Keep audit storage failures from undoing booking operations.
    }
}

}

namespace vrms {

BookingRepository::BookingRepository()
    : connection(createDatabaseConnection()) {}

void BookingRepository::createBooking(
    const std::string& bookingId,
    const std::string& customerId,
    const std::string& vehicleId,
    const std::string& startDate,
    const std::string& endDate
) {
    try {
        booking_validation::validateBooking(
            bookingId,
            customerId,
            vehicleId,
            startDate,
            endDate
        );

        pqxx::work transaction(connection);

        pqxx::result vehicle = transaction.exec(
            "SELECT vehicle_id FROM vehicles "
            "WHERE vehicle_id = $1 FOR UPDATE",
            pqxx::params{vehicleId}
        );

        if (vehicle.empty()) {
            throw std::runtime_error("Vehicle not found");
        }

        pqxx::result overlappingBooking = transaction.exec(
            "SELECT booking_id FROM bookings "
            "WHERE vehicle_id = $1 "
            "AND status IS DISTINCT FROM 'CANCELLED' "
            "AND start_date <= $3::date "
            "AND end_date >= $2::date "
            "AND booking_id <> $4 "
            "LIMIT 1",
            pqxx::params{
                vehicleId,
                startDate,
                endDate,
                bookingId
            }
        );

        if (!overlappingBooking.empty()) {
            throw std::runtime_error(
                "Vehicle is already booked for the requested dates"
            );
        }

        transaction.exec(
            "INSERT INTO bookings "
            "(booking_id, customer_id, vehicle_id, start_date, end_date, status) "
            "VALUES ($1, $2, $3, $4, $5, 'CONFIRMED') "
            "ON CONFLICT (booking_id) DO UPDATE SET "
            "customer_id = EXCLUDED.customer_id, "
            "vehicle_id = EXCLUDED.vehicle_id, "
            "start_date = EXCLUDED.start_date, "
            "end_date = EXCLUDED.end_date, "
            "status = 'CONFIRMED'",
            pqxx::params{
                bookingId,
                customerId,
                vehicleId,
                startDate,
                endDate
            }
        );

        transaction.commit();

        writeBookingAudit(
            customerId,
            "BOOKING_CREATED",
            "Booking " + bookingId + " was created or updated for vehicle " +
                vehicleId + " from " + startDate + " to " + endDate
        );

    } catch (const std::exception& e) {
        writeBookingAudit(
            customerId,
            "BOOKING_CREATE_FAILED",
            "Booking " + bookingId + " could not be created or updated"
        );
        throw;
    }
}

void BookingRepository::cancelBooking(
    const std::string& bookingId
) {
    try {
        pqxx::work transaction(connection);

        pqxx::result owner = transaction.exec(
            "SELECT customer_id FROM bookings WHERE booking_id = $1",
            pqxx::params{bookingId}
        );

        std::string customerId = owner.empty()
            ? "UNKNOWN"
            : owner[0]["customer_id"].as<std::string>();

        pqxx::result result = transaction.exec(
            "UPDATE bookings SET status = 'CANCELLED' "
            "WHERE booking_id = $1",
            pqxx::params{bookingId}
        );

        transaction.commit();

        if (result.affected_rows() > 0) {
            writeBookingAudit(
                customerId,
                "BOOKING_CANCELLED",
                "Booking " + bookingId + " was cancelled"
            );
        } else {
            writeBookingAudit(
                customerId,
                "BOOKING_CANCEL_FAILED",
                "Cancellation requested for booking " + bookingId +
                    ", but no matching booking was found"
            );
        }

    } catch (const std::exception& e) {
        writeBookingAudit(
            "UNKNOWN",
            "BOOKING_CANCEL_FAILED",
            "Booking " + bookingId + " could not be cancelled"
        );
        throw;
    }
}

std::vector<BookingRecord> BookingRepository::getCustomerBookings(
    const std::string& customerId
) {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT booking_id, customer_id, vehicle_id, "
        "start_date, end_date, status "
        "FROM bookings WHERE customer_id = $1 "
        "ORDER BY start_date",
        pqxx::params{customerId}
    );

    std::vector<BookingRecord> bookings;

    for (const auto& row : result) {
        bookings.push_back({
            row["booking_id"].as<std::string>(),
            row["customer_id"].as<std::string>(),
            row["vehicle_id"].as<std::string>(),
            row["start_date"].as<std::string>(),
            row["end_date"].as<std::string>(),
            row["status"].as<std::string>()
        });
    }

    transaction.commit();

    return bookings;
}

std::vector<BookingRecord> BookingRepository::getAllBookings() {
    pqxx::work transaction(connection);
    pqxx::result result = transaction.exec(
        "SELECT booking_id, customer_id, vehicle_id, start_date, end_date, status "
        "FROM bookings ORDER BY start_date DESC, booking_id"
    );

    std::vector<BookingRecord> bookings;
    for (const auto& row : result) {
        bookings.push_back({
            row["booking_id"].as<std::string>(),
            row["customer_id"].as<std::string>(),
            row["vehicle_id"].as<std::string>(),
            row["start_date"].as<std::string>(),
            row["end_date"].as<std::string>(),
            row["status"].as<std::string>()
        });
    }

    transaction.commit();
    return bookings;
}

}
