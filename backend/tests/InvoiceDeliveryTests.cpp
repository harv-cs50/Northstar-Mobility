#include "models/InvoiceDocument.h"
#include "services/EmailService.h"
#include "services/InvoiceDeliveryService.h"
#include "services/InvoicePdfGenerator.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& description) {
    if (!condition) throw std::runtime_error(description);
    std::cout << "[PASS] " << description << '\n';
}

const std::array<const char*, 9> smtpVariables = {
    "VRMS_SMTP_HOST", "VRMS_SMTP_PORT", "VRMS_SMTP_USERNAME",
    "VRMS_SMTP_PASSWORD", "VRMS_SMTP_FROM_EMAIL", "VRMS_SMTP_FROM_NAME",
    "VRMS_SMTP_SECURITY", "VRMS_SMTP_TIMEOUT_SECONDS", "VRMS_INVOICE_TAX_PERCENT"
};

class EnvironmentSnapshot {
public:
    EnvironmentSnapshot() {
        for (const auto* name : smtpVariables) {
            const auto* value = std::getenv(name);
            values.emplace_back(value ? value : "");
            existed.push_back(value != nullptr);
        }
    }
    ~EnvironmentSnapshot() {
        for (std::size_t i = 0; i < smtpVariables.size(); ++i)
            _putenv_s(smtpVariables[i], existed[i] ? values[i].c_str() : "");
    }
private:
    std::vector<std::string> values;
    std::vector<bool> existed;
};

void clearSmtpEnvironment() {
    for (const auto* name : smtpVariables) _putenv_s(name, "");
}

void setValidSmtpEnvironment() {
    const auto nonce = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    _putenv_s("VRMS_SMTP_HOST", "smtp.example.test");
    _putenv_s("VRMS_SMTP_PORT", "587");
    _putenv_s("VRMS_SMTP_USERNAME", "mock-user");
    _putenv_s("VRMS_SMTP_PASSWORD", ("mock-only-" + nonce).c_str());
    _putenv_s("VRMS_SMTP_FROM_EMAIL", "invoices@example.test");
    _putenv_s("VRMS_SMTP_FROM_NAME", "VRMS Test Sender");
    _putenv_s("VRMS_SMTP_SECURITY", "starttls");
    _putenv_s("VRMS_SMTP_TIMEOUT_SECONDS", "5");
}

vrms::InvoiceDocumentData sampleInvoice() {
    vrms::InvoiceDocumentData data;
    data.invoiceId = "INV-TEST-001";
    data.bookingId = "BOOKING-TEST-001";
    data.customerId = "CUSTOMER-TEST-001";
    data.customerName = "Test Customer";
    data.customerEmail = "customer@example.test";
    data.vehicleId = "VH-TEST-01";
    data.vehicleBrand = "Northstar";
    data.vehicleModel = "Tourer";
    data.vehicleType = "Car";
    data.rentalStartDate = "2026-11-20";
    data.rentalEndDate = "2026-11-22";
    data.durationDays = 2;
    data.dailyRate = 125.50;
    data.subtotal = 251.00;
    data.discount = 0.0;
    data.tax = 0.0;
    data.total = 251.00;
    data.paymentStatus = "Successful (simulated)";
    return data;
}

class MockSmtpTransport final : public vrms::SmtpTransport {
public:
    void send(const vrms::SmtpConfig& config, const vrms::EmailMessage& message) override {
        ++sendCount;
        lastConfig = config;
        lastMessage = message;
        if (shouldFail) throw std::runtime_error("mock SMTP rejection");
    }

    bool shouldFail = false;
    int sendCount = 0;
    vrms::SmtpConfig lastConfig;
    vrms::EmailMessage lastMessage;
};

class FakeInvoiceStore final : public vrms::InvoiceDeliveryStore {
public:
    vrms::InvoiceDocumentData loadInvoiceDocument(const std::string& id) override {
        if (!invoicePersisted || id != data.invoiceId)
            throw std::runtime_error("invoice not found in fake store");
        return data;
    }

    bool claimInvoiceEmail(const std::string& id) override {
        if (id != data.invoiceId || (status != "PENDING" && status != "FAILED")) return false;
        status = "SENDING";
        ++attempts;
        return true;
    }

    void markInvoiceEmailSent(const std::string& id) override {
        if (id != data.invoiceId || status != "SENDING")
            throw std::runtime_error("no active claim");
        status = "SENT";
    }

    void markInvoiceEmailFailed(const std::string& id) override {
        if (id != data.invoiceId || status != "SENDING")
            throw std::runtime_error("no active claim");
        status = "FAILED";
    }

    std::string getInvoiceEmailStatus(const std::string&) override { return status; }

    vrms::InvoiceDocumentData data = sampleInvoice();
    bool invoicePersisted = true;
    std::string status = "PENDING";
    int attempts = 0;
};

void testPdfGeneration() {
    const auto pdf = vrms::InvoicePdfGenerator{}.generate(sampleInvoice());
    require(pdf.size() > 800, "Invoice PDF generation returns a complete document");
    require(std::string(pdf.begin(), pdf.begin() + 5) == "%PDF-",
            "Generated invoice begins with a valid PDF signature");
}

void testEmailConfigurationValidation() {
    EnvironmentSnapshot snapshot;
    clearSmtpEnvironment();
    bool missingRejected = false;
    try { (void)vrms::SmtpConfig::fromEnvironment(); }
    catch (const std::runtime_error& error) {
        missingRejected = std::string(error.what()).find("VRMS_SMTP_HOST") != std::string::npos;
    }
    require(missingRejected, "Missing SMTP environment configuration is reported by name");

    setValidSmtpEnvironment();
    const auto valid = vrms::SmtpConfig::fromEnvironment();
    require(valid.port == 587 && valid.security == vrms::SmtpSecurity::StartTls,
            "Valid SMTP environment settings parse successfully");

    _putenv_s("VRMS_SMTP_PORT", "70000");
    bool invalidPortRejected = false;
    try { (void)vrms::SmtpConfig::fromEnvironment(); }
    catch (const std::invalid_argument&) { invalidPortRejected = true; }
    require(invalidPortRejected, "SMTP port outside the TCP range is rejected");

    setValidSmtpEnvironment();
    _putenv_s("VRMS_SMTP_SECURITY", "insecure-unknown-mode");
    bool invalidSecurityRejected = false;
    try { (void)vrms::SmtpConfig::fromEnvironment(); }
    catch (const std::invalid_argument&) { invalidSecurityRejected = true; }
    require(invalidSecurityRejected, "Unknown SMTP transport security mode is rejected");
}

void testSuccessfulDeliveryAndDeduplication() {
    EnvironmentSnapshot snapshot;
    setValidSmtpEnvironment();
    auto mock = std::make_shared<MockSmtpTransport>();
    vrms::EmailService email(mock);
    FakeInvoiceStore store;
    vrms::InvoiceDeliveryService delivery(store, email);

    const auto first = delivery.sendInvoice(store.data.invoiceId);
    require(first.sent && first.status == "SENT" && store.status == "SENT",
            "Successful mocked SMTP delivery marks the invoice SENT");
    require(mock->sendCount == 1 && mock->lastMessage.recipient == store.data.customerEmail,
            "Invoice email is sent to the registered customer address");
    require(mock->lastMessage.attachment.mimeType == "application/pdf" &&
            mock->lastMessage.attachment.filename == "invoice-INV-TEST-001.pdf" &&
            std::string(mock->lastMessage.attachment.content.begin(),
                        mock->lastMessage.attachment.content.begin() + 5) == "%PDF-",
            "Email attaches the generated PDF with the expected filename and MIME type");

    const auto duplicate = delivery.sendInvoice(store.data.invoiceId);
    require(!duplicate.sent && duplicate.status == "SENT" && mock->sendCount == 1,
            "A repeated delivery request does not send a duplicate email");
}

void testFailurePreservesInvoiceAndAllowsAdminRetry() {
    EnvironmentSnapshot snapshot;
    setValidSmtpEnvironment();
    auto mock = std::make_shared<MockSmtpTransport>();
    mock->shouldFail = true;
    vrms::EmailService email(mock);
    FakeInvoiceStore store;
    vrms::InvoiceDeliveryService delivery(store, email);

    const auto failed = delivery.sendInvoice(store.data.invoiceId);
    require(!failed.sent && failed.status == "FAILED" && store.status == "FAILED",
            "SMTP failure is recorded as FAILED without throwing from delivery workflow");
    require(store.invoicePersisted,
            "Email failure leaves the already-persisted invoice intact");
    require(failed.message.find("administrator can retry") != std::string::npos,
            "Email failure explains that an administrator can retry");

    mock->shouldFail = false;
    const auto retried = delivery.sendInvoice(store.data.invoiceId);
    require(retried.sent && retried.status == "SENT" && store.attempts == 2,
            "A failed invoice email can be retried and reaches SENT");
}

void testMissingConfigurationMarksDeliveryFailed() {
    EnvironmentSnapshot snapshot;
    clearSmtpEnvironment();
    auto mock = std::make_shared<MockSmtpTransport>();
    vrms::EmailService email(mock);
    FakeInvoiceStore store;
    vrms::InvoiceDeliveryService delivery(store, email);
    const auto result = delivery.sendInvoice(store.data.invoiceId);
    require(result.status == "FAILED" && store.invoicePersisted,
            "Missing SMTP configuration fails delivery but preserves the invoice");
    require(mock->sendCount == 0,
            "SMTP transport is not invoked when configuration validation fails");
}

}

int main() {
    try {
        testPdfGeneration();
        testEmailConfigurationValidation();
        testSuccessfulDeliveryAndDeduplication();
        testFailurePreservesInvoiceAndAllowsAdminRetry();
        testMissingConfigurationMarksDeliveryFailed();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
