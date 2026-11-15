#include "DatabaseConfig.h"
#include "models/Booking.h"
#include "repositories/BookingRepository.h"

#include <array>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& description) {
    if (!condition) {
        throw std::runtime_error("[FAIL] " + description);
    }

    std::cout << "[PASS] " << description << '\n';
}

template <typename Action>
void requireInvalidArgument(
    Action&& action,
    const std::string& description
) {
    try {
        action();
    }
    catch (const std::invalid_argument&) {
        require(true, description);
        return;
    }
    catch (const std::exception& error) {
        throw std::runtime_error(
            "[FAIL] " + description + ": unexpected exception: " +
            error.what()
        );
    }

    throw std::runtime_error("[FAIL] " + description);
}

void runModelTests() {
    vrms::Booking validLeapDay(
        "B-MODEL-1",
        "C-MODEL-1",
        "V-MODEL-1",
        "2024-02-29",
        "2024-03-01"
    );
    require(
        validLeapDay.getStartDate() == "2024-02-29",
        "Model accepts a real leap-day booking date"
    );

    requireInvalidArgument(
        [] {
            vrms::Booking booking(
                "B-MODEL-2", "C-MODEL-1", "V-MODEL-1",
                "2024-2-09", "2024-02-10"
            );
        },
        "Model rejects a non-YYYY-MM-DD date"
    );

    requireInvalidArgument(
        [] {
            vrms::Booking booking(
                "B-MODEL-3", "C-MODEL-1", "V-MODEL-1",
                "2023-02-29", "2023-03-01"
            );
        },
        "Model rejects an invalid calendar date"
    );

    requireInvalidArgument(
        [] {
            vrms::Booking booking(
                "B-MODEL-4", "C-MODEL-1", "V-MODEL-1",
                "0000-01-01", "0000-01-02"
            );
        },
        "Model rejects year 0000"
    );

    requireInvalidArgument(
        [] {
            vrms::Booking booking(
                "B-MODEL-5", "C-MODEL-1", "V-MODEL-1",
                "2024-04-31", "2024-05-01"
            );
        },
        "Model rejects an impossible month/day combination"
    );

    requireInvalidArgument(
        [] {
            vrms::Booking booking(
                "B-MODEL-6", "C-MODEL-1", "V-MODEL-1",
                "2025-04-03", "2025-04-02"
            );
        },
        "Model rejects an end date before the start date"
    );

    vrms::Booking pending(
        "B-MODEL-7", "C-MODEL-1", "V-MODEL-1",
        "2025-05-01", "2025-05-03"
    );
    vrms::Booking confirmed(
        "B-MODEL-8", "C-MODEL-2", "V-MODEL-1",
        "2025-05-03", "2025-05-04"
    );
    confirmed.confirmBooking();
    require(
        pending.overlapsWith(confirmed),
        "Pending and confirmed bookings both block inclusive overlaps"
    );

    confirmed.cancelBooking();
    require(
        !pending.overlapsWith(confirmed),
        "Cancelled bookings do not block model availability"
    );
}

bool hasBooking(
    const std::vector<vrms::BookingRecord>& bookings,
    const std::string& bookingId
) {
    for (const auto& booking : bookings) {
        if (booking.bookingId == bookingId) {
            return true;
        }
    }

    return false;
}

void requireOverlapRejected(
    vrms::BookingRepository& repository,
    const std::string& bookingId,
    const std::string& customerId,
    const std::string& vehicleId,
    const std::string& startDate,
    const std::string& endDate,
    const std::string& description
) {
    try {
        repository.createBooking(
            bookingId,
            customerId,
            vehicleId,
            startDate,
            endDate
        );
    }
    catch (const std::exception& error) {
        if (
            std::string(error.what()).find(
                "already booked for the requested dates"
            ) != std::string::npos
        ) {
            require(true, description);
            return;
        }

        throw std::runtime_error(
            "[FAIL] " + description + ": unexpected exception: " +
            error.what()
        );
    }

    throw std::runtime_error("[FAIL] " + description);
}

void requireTestDatabase() {
    const char* allowTestDatabase =
        std::getenv("VRMS_ALLOW_BOOKING_TEST_DB");
    const char* databaseName = std::getenv("PGDATABASE");
    const char* host = std::getenv("PGHOST");

    if (
        allowTestDatabase == nullptr ||
        std::string(allowTestDatabase) != "1" ||
        databaseName == nullptr ||
        std::string(databaseName) != "vrms_booking_test" ||
        host == nullptr ||
        std::string(host) != "127.0.0.1"
    ) {
        throw std::runtime_error(
            "Repository tests require VRMS_ALLOW_BOOKING_TEST_DB=1, "
            "PGHOST=127.0.0.1, and PGDATABASE=vrms_booking_test"
        );
    }
}

void ensureTestSchema(pqxx::connection& connection) {
    pqxx::work transaction(connection);

    transaction.exec(
        "CREATE TABLE IF NOT EXISTS branches ("
        "branch_id integer GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY, "
        "branch_name character varying NOT NULL, "
        "city character varying NOT NULL, "
        "address text NOT NULL)"
    );
    transaction.exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "user_id character varying PRIMARY KEY, "
        "name character varying NOT NULL, "
        "email character varying NOT NULL UNIQUE, "
        "password_hash text NOT NULL, "
        "role character varying NOT NULL "
            "CHECK (role IN ('CUSTOMER', 'ADMIN')), "
        "membership_level character varying DEFAULT 'Regular', "
        "created_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP)"
    );
    transaction.exec(
        "CREATE TABLE IF NOT EXISTS vehicles ("
        "vehicle_id character varying PRIMARY KEY, "
        "brand character varying NOT NULL, "
        "model character varying NOT NULL, "
        "vehicle_type character varying NOT NULL, "
        "daily_rate numeric NOT NULL CHECK (daily_rate > 0), "
        "is_available boolean DEFAULT true, "
        "branch_id integer REFERENCES branches(branch_id))"
    );
    transaction.exec(
        "CREATE TABLE IF NOT EXISTS bookings ("
        "booking_id character varying PRIMARY KEY, "
        "customer_id character varying REFERENCES users(user_id), "
        "vehicle_id character varying REFERENCES vehicles(vehicle_id), "
        "start_date date NOT NULL, "
        "end_date date NOT NULL, "
        "status character varying DEFAULT 'PENDING', "
        "CHECK (end_date >= start_date), "
        "CHECK (status IN ('PENDING', 'CONFIRMED', 'CANCELLED')))"
    );
    transaction.exec(
        "CREATE TABLE IF NOT EXISTS audit_logs ("
        "log_id character varying PRIMARY KEY, "
        "user_id character varying NOT NULL, "
        "action character varying NOT NULL, "
        "description text NOT NULL, "
        "created_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP)"
    );

    transaction.commit();
}

void runRepositoryTests() {
    requireTestDatabase();

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    const std::string suffix = std::to_string(timestamp);
    const std::string customerId = "booking-test-customer-" + suffix;
    const std::string vehicleOneId = "booking-test-vehicle-1-" + suffix;
    const std::string vehicleTwoId = "booking-test-vehicle-2-" + suffix;

    auto setupConnection = vrms::createDatabaseConnection();
    ensureTestSchema(setupConnection);
    {
        pqxx::work transaction(setupConnection);
        transaction.exec(
            "INSERT INTO users "
            "(user_id, name, email, password_hash, role, membership_level) "
            "VALUES ($1, 'Booking Test', $2, 'test-hash', 'CUSTOMER', 'Regular')",
            pqxx::params{
                customerId,
                "booking-test-" + suffix + "@example.test"
            }
        );

        for (const auto& vehicleId : {vehicleOneId, vehicleTwoId}) {
            transaction.exec(
                "INSERT INTO vehicles "
                "(vehicle_id, brand, model, vehicle_type, daily_rate) "
                "VALUES ($1, 'Test', 'Vehicle', 'Car', 10.00)",
                pqxx::params{vehicleId}
            );
        }

        transaction.commit();
    }

    vrms::BookingRepository repository;
    const std::string normalId = "booking-test-normal-" + suffix;
    repository.createBooking(
        normalId,
        customerId,
        vehicleOneId,
        "2035-01-01",
        "2035-01-03"
    );
    auto bookings = repository.getCustomerBookings(customerId);
    require(
        hasBooking(bookings, normalId),
        "Repository accepts and persists a normal booking"
    );

    const std::string overlappingId = "booking-test-overlap-" + suffix;
    requireOverlapRejected(
        repository,
        overlappingId,
        customerId,
        vehicleOneId,
        "2035-01-03",
        "2035-01-04",
        "Repository rejects an inclusive overlapping booking"
    );
    bookings = repository.getCustomerBookings(customerId);
    require(
        !hasBooking(bookings, overlappingId),
        "Rejected overlap leaves no booking row"
    );

    const std::string nonOverlappingId =
        "booking-test-nonoverlap-" + suffix;
    repository.createBooking(
        nonOverlappingId,
        customerId,
        vehicleOneId,
        "2035-01-04",
        "2035-01-05"
    );
    bookings = repository.getCustomerBookings(customerId);
    require(
        hasBooking(bookings, nonOverlappingId),
        "Repository accepts a non-overlapping booking"
    );

    repository.cancelBooking(normalId);
    const std::string afterCancellationId =
        "booking-test-after-cancel-" + suffix;
    repository.createBooking(
        afterCancellationId,
        customerId,
        vehicleOneId,
        "2035-01-01",
        "2035-01-03"
    );
    bookings = repository.getCustomerBookings(customerId);
    require(
        hasBooking(bookings, afterCancellationId),
        "Cancelled booking does not block the same dates"
    );

    const std::string pendingId = "booking-test-pending-" + suffix;
    {
        pqxx::work transaction(setupConnection);
        transaction.exec(
            "INSERT INTO bookings "
            "(booking_id, customer_id, vehicle_id, start_date, end_date, status) "
            "VALUES ($1, $2, $3, '2035-06-01', '2035-06-03', 'PENDING')",
            pqxx::params{pendingId, customerId, vehicleOneId}
        );
        transaction.commit();
    }

    requireOverlapRejected(
        repository,
        "booking-test-pending-overlap-" + suffix,
        customerId,
        vehicleOneId,
        "2035-06-02",
        "2035-06-04",
        "Pending booking blocks an overlapping request"
    );

    requireInvalidArgument(
        [&] {
            repository.createBooking(
                "booking-test-invalid-date-" + suffix,
                customerId,
                vehicleOneId,
                "2035-02-30",
                "2035-03-01"
            );
        },
        "Repository rejects an invalid calendar date"
    );
    requireInvalidArgument(
        [&] {
            repository.createBooking(
                "booking-test-reversed-date-" + suffix,
                customerId,
                vehicleOneId,
                "2035-07-02",
                "2035-07-01"
            );
        },
        "Repository rejects an end date before the start date"
    );
    bookings = repository.getCustomerBookings(customerId);
    require(
        !hasBooking(bookings, "booking-test-invalid-date-" + suffix) &&
            !hasBooking(bookings, "booking-test-reversed-date-" + suffix),
        "Invalid booking attempts leave no booking rows"
    );

    vrms::BookingRepository firstRepository;
    vrms::BookingRepository secondRepository;
    std::barrier startTogether(3);
    std::array<int, 2> outcomes = {0, 0};
    std::array<std::string, 2> errors;

    auto concurrentAttempt = [&](std::size_t index,
                                 vrms::BookingRepository& concurrentRepository) {
        startTogether.arrive_and_wait();
        try {
            concurrentRepository.createBooking(
                "booking-test-concurrent-" + suffix + "-" +
                    std::to_string(index),
                customerId,
                vehicleTwoId,
                "2035-08-01",
                "2035-08-03"
            );
            outcomes[index] = 1;
        }
        catch (const std::exception& error) {
            errors[index] = error.what();
            outcomes[index] =
                errors[index].find(
                    "already booked for the requested dates"
                ) != std::string::npos
                    ? -1
                    : -2;
        }
    };

    std::thread firstRequest(
        concurrentAttempt,
        0,
        std::ref(firstRepository)
    );
    std::thread secondRequest(
        concurrentAttempt,
        1,
        std::ref(secondRepository)
    );
    startTogether.arrive_and_wait();
    firstRequest.join();
    secondRequest.join();

    const int successCount =
        (outcomes[0] == 1 ? 1 : 0) + (outcomes[1] == 1 ? 1 : 0);
    const int overlapRejectionCount =
        (outcomes[0] == -1 ? 1 : 0) + (outcomes[1] == -1 ? 1 : 0);
    require(
        successCount == 1 && overlapRejectionCount == 1,
        "Concurrent overlapping requests produce exactly one success"
    );

    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (outcomes[index] == -2) {
            throw std::runtime_error(
                "[FAIL] Concurrent request failed unexpectedly: " +
                errors[index]
            );
        }
    }
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr
                << "Usage: BookingTests --model-only|--repository\n";
            return 2;
        }

        runModelTests();

        const std::string mode = argv[1];
        if (mode == "--model-only") {
            return 0;
        }
        if (mode == "--repository") {
            runRepositoryTests();
            return 0;
        }

        std::cerr << "Unknown BookingTests mode: " << mode << '\n';
        return 2;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
