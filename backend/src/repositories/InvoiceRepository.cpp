
#include "repositories/InvoiceRepository.h"
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

std::string generateInvoiceAuditId() {
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

void writeInvoiceAudit(
    const std::string& userId,
    const std::string& action,
    const std::string& description
) noexcept {

    try {
        vrms::AuditLogRepository repository;

        vrms::AuditLog log(
            generateInvoiceAuditId(),
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

template <typename Row>
vrms::InvoiceRecord invoiceFromRow(const Row& row) {
    return {
        row["invoice_id"].template as<std::string>(),
        row["booking_id"].template as<std::string>(),
        row["subtotal"].template as<double>(),
        row["discount"].template as<double>(),
        row["tax"].template as<double>(),
        row["total_amount"].template as<double>(),
        row["email_status"].template as<std::string>(),
        row["email_attempts"].template as<int>(),
        row["email_last_attempt_at"].is_null()
            ? "" : row["email_last_attempt_at"].template as<std::string>(),
        row["email_sent_at"].is_null()
            ? "" : row["email_sent_at"].template as<std::string>(),
        row["email_last_error"].is_null()
            ? "" : row["email_last_error"].template as<std::string>()
    };
}

constexpr const char* invoiceSelectColumns =
    "invoice_id, booking_id, subtotal, discount, tax, total_amount, "
    "email_status, email_attempts, email_last_attempt_at, email_sent_at, email_last_error";

}

namespace vrms {

InvoiceRepository::InvoiceRepository()
    : connection(createDatabaseConnection()) {}

void InvoiceRepository::saveInvoice(
    const std::string& invoiceId,
    const std::string& bookingId,
    double subtotal,
    double discount,
    double tax,
    double totalAmount
) {

    if (
        invoiceId.empty() ||
        bookingId.empty() ||
        subtotal <= 0 ||
        discount < 0 ||
        tax < 0 ||
        totalAmount <= 0
    ) {
        writeInvoiceAudit(
            "UNKNOWN",
            "INVOICE_SAVE_FAILED",
            "Invoice " + invoiceId +
            " rejected due to invalid invoice details"
        );

        throw std::invalid_argument("Invalid invoice details");
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
            "INSERT INTO invoices "
            "(invoice_id, booking_id, subtotal, discount, tax, total_amount) "
            "VALUES ($1, $2, $3, $4, $5, $6) "
            "ON CONFLICT (invoice_id) DO UPDATE SET "
            "booking_id = EXCLUDED.booking_id, "
            "subtotal = EXCLUDED.subtotal, "
            "discount = EXCLUDED.discount, "
            "tax = EXCLUDED.tax, "
            "total_amount = EXCLUDED.total_amount",

            pqxx::params{
                invoiceId,
                bookingId,
                subtotal,
                discount,
                tax,
                totalAmount
            }
        );

        transaction.commit();

        writeInvoiceAudit(
            customerId,
            "INVOICE_SAVED",
            "Invoice " + invoiceId +
            " for booking " + bookingId +
            " saved successfully. Total amount: Rs. " +
            std::to_string(totalAmount)
        );

    }
    catch (const std::exception& e) {

        writeInvoiceAudit(
            customerId,
            "INVOICE_SAVE_FAILED",
            "Invoice " + invoiceId +
            " for booking " + bookingId +
            " could not be saved"
        );

        throw;
    }
}

InvoiceCreateResult InvoiceRepository::createOrGetInvoice(const Invoice& invoice) {
    std::string customerId;
    try {
        pqxx::work transaction(connection);
        const auto booking = transaction.exec(
            "SELECT customer_id FROM bookings WHERE booking_id = $1 FOR UPDATE",
            pqxx::params{invoice.getBookingId()}
        );
        if (booking.empty()) throw std::runtime_error("Booking not found for invoice");
        customerId = booking[0]["customer_id"].as<std::string>();

        const auto existing = transaction.exec(
            std::string("SELECT ") + invoiceSelectColumns +
                " FROM invoices WHERE booking_id = $1 ORDER BY invoice_id LIMIT 1",
            pqxx::params{invoice.getBookingId()}
        );
        if (!existing.empty()) {
            InvoiceCreateResult result{invoiceFromRow(existing[0]), false};
            transaction.commit();
            return result;
        }

        transaction.exec(
            "INSERT INTO invoices "
            "(invoice_id, booking_id, subtotal, discount, tax, total_amount, email_status, email_attempts) "
            "VALUES ($1, $2, $3, $4, $5, $6, 'PENDING', 0)",
            pqxx::params{
                invoice.getInvoiceId(),
                invoice.getBookingId(),
                invoice.calculateTotal() + invoice.calculateDiscount() - invoice.calculateTax(),
                invoice.calculateDiscount(),
                invoice.calculateTax(),
                invoice.calculateTotal()
            }
        );

        const auto saved = transaction.exec(
            std::string("SELECT ") + invoiceSelectColumns +
                " FROM invoices WHERE booking_id = $1 ORDER BY invoice_id LIMIT 1",
            pqxx::params{invoice.getBookingId()}
        );
        InvoiceCreateResult result{invoiceFromRow(saved[0]), true};
        transaction.commit();

        writeInvoiceAudit(
            customerId,
            "INVOICE_SAVED",
            "Invoice " + result.invoice.invoiceId + " created for booking " +
                invoice.getBookingId() + " with total amount " +
                std::to_string(result.invoice.totalAmount)
        );
        return result;
    } catch (const std::exception&) {
        writeInvoiceAudit(
            customerId,
            "INVOICE_SAVE_FAILED",
            "Invoice for booking " + invoice.getBookingId() + " could not be created"
        );
        throw;
    }
}

InvoiceRecord InvoiceRepository::getInvoiceById(
    const std::string& invoiceId
) {

    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        std::string("SELECT ") + invoiceSelectColumns + " FROM invoices WHERE invoice_id = $1",
        pqxx::params{invoiceId}
    );

    if (result.empty()) {
        throw std::runtime_error("Invoice not found");
    }

    InvoiceRecord invoice = invoiceFromRow(result[0]);

    transaction.commit();

    return invoice;
}

std::vector<InvoiceRecord> InvoiceRepository::getAllInvoices() {

    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        std::string("SELECT ") + invoiceSelectColumns + " FROM invoices ORDER BY invoice_id"
    );

    std::vector<InvoiceRecord> invoices;

    for (const auto& row : result) {
        invoices.push_back(invoiceFromRow(row));
    }

    transaction.commit();

    return invoices;
}

std::vector<InvoiceRecord> InvoiceRepository::getCustomerInvoices(
    const std::string& customerId) {
    if (customerId.empty())
        throw std::invalid_argument("Customer ID is required");

    pqxx::read_transaction transaction(connection);
    const auto result = transaction.exec(
        "SELECT i.invoice_id, i.booking_id, i.email_status "
        " FROM invoices i JOIN bookings b ON b.booking_id = i.booking_id "
        "WHERE b.customer_id = $1 ORDER BY i.invoice_id",
        pqxx::params{customerId}
    );

    std::vector<InvoiceRecord> invoices;
    for (const auto& row : result) {
        InvoiceRecord invoice{};
        invoice.invoiceId = row["invoice_id"].as<std::string>();
        invoice.bookingId = row["booking_id"].as<std::string>();
        invoice.emailStatus = row["email_status"].as<std::string>();
        invoices.push_back(std::move(invoice));
    }
    transaction.commit();
    return invoices;
}

InvoiceDocumentData InvoiceRepository::getInvoiceDocument(
    const std::string& invoiceId
) {
    pqxx::read_transaction transaction(connection);
    const auto result = transaction.exec(
        "SELECT i.invoice_id, i.booking_id, i.subtotal, i.discount, i.tax, i.total_amount, "
        "b.customer_id, u.name AS customer_name, u.email AS customer_email, "
        "v.vehicle_id, v.brand, v.model, v.vehicle_type, v.daily_rate, "
        "to_char(b.start_date, 'YYYY-MM-DD') AS start_date, "
        "to_char(b.end_date, 'YYYY-MM-DD') AS end_date, "
        "GREATEST(b.end_date - b.start_date, 1) AS duration_days, "
        "COALESCE(p.payment_status, 'Unknown') AS payment_status "
        "FROM invoices i "
        "JOIN bookings b ON b.booking_id = i.booking_id "
        "JOIN users u ON u.user_id = b.customer_id "
        "JOIN vehicles v ON v.vehicle_id = b.vehicle_id "
        "LEFT JOIN LATERAL ("
        " SELECT payment_status FROM payments WHERE booking_id = b.booking_id "
        " ORDER BY payment_id DESC LIMIT 1"
        ") p ON TRUE "
        "WHERE i.invoice_id = $1",
        pqxx::params{invoiceId}
    );
    if (result.empty()) throw std::runtime_error("Invoice document not found");

    const auto& row = result[0];
    InvoiceDocumentData data;
    data.invoiceId = row["invoice_id"].as<std::string>();
    data.bookingId = row["booking_id"].as<std::string>();
    data.customerId = row["customer_id"].as<std::string>();
    data.customerName = row["customer_name"].as<std::string>();
    data.customerEmail = row["customer_email"].as<std::string>();
    data.vehicleId = row["vehicle_id"].as<std::string>();
    data.vehicleBrand = row["brand"].as<std::string>();
    data.vehicleModel = row["model"].as<std::string>();
    data.vehicleType = row["vehicle_type"].as<std::string>();
    data.rentalStartDate = row["start_date"].as<std::string>();
    data.rentalEndDate = row["end_date"].as<std::string>();
    data.durationDays = row["duration_days"].as<int>();
    data.dailyRate = row["daily_rate"].as<double>();
    data.subtotal = row["subtotal"].as<double>();
    data.discount = row["discount"].as<double>();
    data.tax = row["tax"].as<double>();
    data.total = row["total_amount"].as<double>();
    data.paymentStatus = row["payment_status"].as<std::string>();
    return data;
}

bool InvoiceRepository::claimInvoiceEmail(const std::string& invoiceId) {
    pqxx::work transaction(connection);
    const auto result = transaction.exec(
        "UPDATE invoices SET email_status = 'SENDING', "
        "email_attempts = email_attempts + 1, "
        "email_last_attempt_at = CURRENT_TIMESTAMP, email_last_error = NULL "
        "WHERE invoice_id = $1 AND email_status IN ('PENDING', 'FAILED')",
        pqxx::params{invoiceId}
    );
    transaction.commit();
    return result.affected_rows() == 1;
}

void InvoiceRepository::markInvoiceEmailSent(const std::string& invoiceId) {
    pqxx::work transaction(connection);
    const auto result = transaction.exec(
        "UPDATE invoices SET email_status = 'SENT', email_sent_at = CURRENT_TIMESTAMP, "
        "email_last_error = NULL WHERE invoice_id = $1 AND email_status = 'SENDING'",
        pqxx::params{invoiceId}
    );
    if (result.affected_rows() != 1)
        throw std::runtime_error("Invoice email delivery claim was lost before marking sent");
    transaction.commit();
}

void InvoiceRepository::markInvoiceEmailFailed(const std::string& invoiceId) {
    pqxx::work transaction(connection);
    const auto result = transaction.exec(
        "UPDATE invoices SET email_status = 'FAILED', email_last_error = $1 "
        "WHERE invoice_id = $2 AND email_status = 'SENDING'",
        pqxx::params{
            std::string("SMTP delivery failed; check server logs and configuration"),
            invoiceId
        }
    );
    if (result.affected_rows() != 1)
        throw std::runtime_error("Invoice email delivery state could not be marked failed");
    transaction.commit();
}

std::string InvoiceRepository::getInvoiceEmailStatus(const std::string& invoiceId) {
    pqxx::read_transaction transaction(connection);
    const auto result = transaction.exec(
        "SELECT email_status FROM invoices WHERE invoice_id = $1",
        pqxx::params{invoiceId}
    );
    if (result.empty()) throw std::runtime_error("Invoice not found");
    return result[0]["email_status"].as<std::string>();
}

}
