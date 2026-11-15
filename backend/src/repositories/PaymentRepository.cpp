
#include "repositories/PaymentRepository.h"
#include "DatabaseConfig.h"
#include "repositories/AuditLogRepository.h"
#include "models/AuditLog.h"

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace {

std::string generatePaymentAuditId() {
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

void writePaymentAudit(
    const std::string& userId,
    const std::string& action,
    const std::string& description
) noexcept {

    try {
        vrms::AuditLogRepository repository;

        vrms::AuditLog log(
            generatePaymentAuditId(),
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
                  << e.what()
                  << "\n";
    }
    catch (...) {

        std::cerr << "[AUDIT ERROR] Unknown error occurred.\n";
    }
}

}

namespace vrms {

PaymentRepository::PaymentRepository()
    : connection(createDatabaseConnection()) {}

void PaymentRepository::savePayment(
    const std::string& paymentId,
    const std::string& bookingId,
    double amount,
    const std::string& method,
    const std::string& status
) {

    if (
        paymentId.empty() ||
        bookingId.empty() ||
        amount <= 0 ||
        method.empty() ||
        status.empty()
    ) {

        writePaymentAudit(
            "UNKNOWN",
            "PAYMENT_SAVE_FAILED",
            "Payment " + paymentId +
            " rejected due to invalid payment details"
        );

        throw std::invalid_argument("Invalid payment details");
    }

    try {

        pqxx::work transaction(connection);

        pqxx::result bookingResult = transaction.exec(
            "SELECT customer_id FROM bookings WHERE booking_id = $1 FOR UPDATE",
            pqxx::params{bookingId}
        );

        if (bookingResult.empty()) {
            throw std::runtime_error("Booking not found for payment");
        }

        if (status == "Successful") {
            const auto existingSuccessfulPayment = transaction.exec(
                "SELECT payment_id FROM payments WHERE booking_id = $1 "
                "AND payment_status = 'Successful' AND payment_id <> $2 LIMIT 1",
                pqxx::params{bookingId, paymentId}
            );
            if (!existingSuccessfulPayment.empty()) {
                throw std::runtime_error("Booking already has a successful payment");
            }
        }

        std::string customerId = bookingResult[0]["customer_id"].as<std::string>();

        transaction.exec(
            "INSERT INTO payments "
            "(payment_id, booking_id, amount, payment_method, payment_status) "
            "VALUES ($1, $2, $3, $4, $5) "
            "ON CONFLICT (payment_id) DO UPDATE SET "
            "amount = EXCLUDED.amount, "
            "payment_method = EXCLUDED.payment_method, "
            "payment_status = EXCLUDED.payment_status",

            pqxx::params{
                paymentId,
                bookingId,
                amount,
                method,
                status
            }
        );

        transaction.commit();

        writePaymentAudit(
            customerId,
            "PAYMENT_SAVED",
            "Payment " + paymentId +
            " for booking " + bookingId +
            " recorded with amount " +
            std::to_string(amount) +
            ", method " + method +
            ", status " + status
        );

    }
    catch (const std::exception& e) {

        writePaymentAudit(
            "UNKNOWN",
            "PAYMENT_SAVE_FAILED",
            "Payment " + paymentId +
            " for booking " + bookingId +
            " could not be saved"
        );

        throw;
    }
}

PaymentRecord PaymentRepository::getPaymentById(
    const std::string& paymentId
) {

    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT payment_id, booking_id, amount, "
        "payment_method, payment_status "
        "FROM payments WHERE payment_id = $1",

        pqxx::params{paymentId}
    );

    if (result.empty()) {
        throw std::runtime_error("Payment not found");
    }

    const auto& row = result[0];

    PaymentRecord payment{
        row["payment_id"].as<std::string>(),
        row["booking_id"].as<std::string>(),
        row["amount"].as<double>(),
        row["payment_method"].as<std::string>(),
        row["payment_status"].as<std::string>()
    };

    transaction.commit();

    return payment;
}

std::vector<PaymentRecord> PaymentRepository::getAllPayments() {

    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT payment_id, booking_id, amount, "
        "payment_method, payment_status "
        "FROM payments ORDER BY payment_id"
    );

    std::vector<PaymentRecord> payments;

    for (const auto& row : result) {

        payments.push_back({
            row["payment_id"].as<std::string>(),
            row["booking_id"].as<std::string>(),
            row["amount"].as<double>(),
            row["payment_method"].as<std::string>(),
            row["payment_status"].as<std::string>()
        });
    }

    transaction.commit();

    return payments;
}

std::vector<PaymentRecord> PaymentRepository::getCustomerPayments(
    const std::string& customerId) {
    if (customerId.empty())
        throw std::invalid_argument("Customer ID is required");

    pqxx::read_transaction transaction(connection);
    const auto result = transaction.exec(
        "SELECT p.payment_id, p.booking_id, p.amount, "
        "''::text AS payment_method, p.payment_status "
        "FROM payments p JOIN bookings b ON b.booking_id = p.booking_id "
        "WHERE b.customer_id = $1 ORDER BY p.payment_id",
        pqxx::params{customerId}
    );

    std::vector<PaymentRecord> payments;
    for (const auto& row : result) {
        payments.push_back({
            row["payment_id"].as<std::string>(),
            row["booking_id"].as<std::string>(),
            row["amount"].as<double>(),
            row["payment_method"].as<std::string>(),
            row["payment_status"].as<std::string>()
        });
    }
    transaction.commit();
    return payments;
}

std::optional<PaymentRecord> PaymentRepository::getSuccessfulPaymentForBooking(
    const std::string& bookingId
) {
    pqxx::read_transaction transaction(connection);
    const auto result = transaction.exec(
        "SELECT payment_id, booking_id, amount, payment_method, payment_status "
        "FROM payments WHERE booking_id = $1 AND payment_status = 'Successful' "
        "ORDER BY payment_id DESC LIMIT 1",
        pqxx::params{bookingId}
    );
    if (result.empty()) return std::nullopt;
    const auto& row = result[0];
    return PaymentRecord{
        row["payment_id"].as<std::string>(),
        row["booking_id"].as<std::string>(),
        row["amount"].as<double>(),
        row["payment_method"].as<std::string>(),
        row["payment_status"].as<std::string>()
    };
}

void PaymentRepository::updatePaymentStatus(
    const std::string& paymentId,
    const std::string& status
) {

    if (paymentId.empty() || status.empty()) {

        writePaymentAudit(
            "UNKNOWN",
            "PAYMENT_STATUS_UPDATE_FAILED",
            "Payment status update rejected due to invalid details"
        );

        throw std::invalid_argument(
            "Invalid payment update details"
        );
    }

    try {

        pqxx::work transaction(connection);

        pqxx::result paymentResult = transaction.exec(
            "SELECT b.customer_id, p.booking_id "
            "FROM payments p "
            "JOIN bookings b ON b.booking_id = p.booking_id "
            "WHERE p.payment_id = $1",

            pqxx::params{paymentId}
        );

        if (paymentResult.empty()) {
            throw std::runtime_error("Payment not found");
        }

        std::string customerId =
            paymentResult[0]["customer_id"].as<std::string>();

        transaction.exec(
            "UPDATE payments SET payment_status = $1 "
            "WHERE payment_id = $2",

            pqxx::params{
                status,
                paymentId
            }
        );

        transaction.commit();

        writePaymentAudit(
            customerId,
            "PAYMENT_STATUS_UPDATED",
            "Payment " + paymentId +
            " status changed to " + status
        );

    }
    catch (const std::exception& e) {

        writePaymentAudit(
            "UNKNOWN",
            "PAYMENT_STATUS_UPDATE_FAILED",
            "Payment " + paymentId +
            " status could not be updated"
        );

        throw;
    }
}

}
