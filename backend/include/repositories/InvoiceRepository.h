
#ifndef INVOICE_REPOSITORY_H
#define INVOICE_REPOSITORY_H

#include "models/Invoice.h"
#include "models/InvoiceDocument.h"

#include <pqxx/pqxx>
#include <optional>
#include <string>
#include <vector>

namespace vrms {

struct InvoiceRecord {
    std::string invoiceId;
    std::string bookingId;
    double subtotal;
    double discount;
    double tax;
    double totalAmount;
    std::string emailStatus = "PENDING";
    int emailAttempts = 0;
    std::string emailLastAttemptAt;
    std::string emailSentAt;
    std::string emailLastError;
};

struct InvoiceCreateResult {
    InvoiceRecord invoice;
    bool created = false;
};

class InvoiceRepository {
private:
    pqxx::connection connection;

public:
    InvoiceRepository();

    void saveInvoice(
        const std::string& invoiceId,
        const std::string& bookingId,
        double subtotal,
        double discount,
        double tax,
        double totalAmount
    );

    InvoiceCreateResult createOrGetInvoice(const Invoice& invoice);

    InvoiceRecord getInvoiceById(
        const std::string& invoiceId
    );

    std::vector<InvoiceRecord> getAllInvoices();

    // Returns only invoices whose booking belongs to the supplied customer.
    std::vector<InvoiceRecord> getCustomerInvoices(
        const std::string& customerId
    );

    InvoiceDocumentData getInvoiceDocument(const std::string& invoiceId);
    bool claimInvoiceEmail(const std::string& invoiceId);
    void markInvoiceEmailSent(const std::string& invoiceId);
    void markInvoiceEmailFailed(const std::string& invoiceId);
    std::string getInvoiceEmailStatus(const std::string& invoiceId);
};

}

#endif
