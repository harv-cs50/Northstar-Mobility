#include "services/InvoiceDeliveryService.h"

#include <iostream>
#include <stdexcept>

namespace vrms {

InvoiceDeliveryService::InvoiceDeliveryService(
    InvoiceDeliveryStore& invoiceStore,
    EmailService& smtpService
) : store(invoiceStore), emailService(smtpService) {}

InvoiceDeliveryResult InvoiceDeliveryService::sendInvoice(
    const std::string& invoiceId
) {
    if (!store.claimInvoiceEmail(invoiceId)) {
        return {
            store.getInvoiceEmailStatus(invoiceId),
            "Invoice email is already sent or another delivery attempt is active.",
            false
        };
    }

    InvoiceDocumentData document;
    std::vector<std::uint8_t> pdf;
    try {
        document = store.loadInvoiceDocument(invoiceId);
        pdf = pdfGenerator.generate(document);
        emailService.sendInvoice(document.customerEmail, invoiceId, pdf);
    } catch (const std::exception& error) {
        std::cerr << "[INVOICE EMAIL] Delivery failed for " << invoiceId
                  << ": " << error.what() << '\n';
        try {
            store.markInvoiceEmailFailed(invoiceId);
            return {
                "FAILED",
                "Payment and invoice are saved, but email delivery failed. An administrator can retry it.",
                false
            };
        } catch (const std::exception& statusError) {
            std::cerr << "[INVOICE EMAIL] Could not persist failure status for "
                      << invoiceId << ": " << statusError.what() << '\n';
            return {
                "SENDING",
                "Invoice is saved; delivery status could not be updated. Check the admin invoice list before retrying.",
                false
            };
        }
    } catch (...) {
        try {
            store.markInvoiceEmailFailed(invoiceId);
        } catch (...) {
            return {"SENDING", "Invoice is saved; delivery status could not be updated.", false};
        }
        return {
            "FAILED",
            "Payment and invoice are saved, but email delivery failed. An administrator can retry it.",
            false
        };
    }

    try {
        store.markInvoiceEmailSent(invoiceId);
    } catch (const std::exception& error) {
        // SMTP accepted the message. Keep SENDING so normal retries do not send it twice.
        std::cerr << "[INVOICE EMAIL] SMTP accepted " << invoiceId
                  << " but delivery status could not be persisted: " << error.what() << '\n';
        return {
            "SENDING",
            "The email server accepted the invoice, but delivery status could not be updated. Do not resend until status is checked.",
            false
        };
    }

    return {"SENT", "Invoice PDF emailed to the customer's registered email address.", true};
}

}
