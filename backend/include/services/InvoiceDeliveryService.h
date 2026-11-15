#ifndef INVOICE_DELIVERY_SERVICE_H
#define INVOICE_DELIVERY_SERVICE_H

#include "models/InvoiceDocument.h"
#include "services/EmailService.h"
#include "services/InvoicePdfGenerator.h"

#include <string>

namespace vrms {

class InvoiceDeliveryStore {
public:
    virtual ~InvoiceDeliveryStore() = default;
    virtual InvoiceDocumentData loadInvoiceDocument(const std::string& invoiceId) = 0;
    virtual bool claimInvoiceEmail(const std::string& invoiceId) = 0;
    virtual void markInvoiceEmailSent(const std::string& invoiceId) = 0;
    virtual void markInvoiceEmailFailed(const std::string& invoiceId) = 0;
    virtual std::string getInvoiceEmailStatus(const std::string& invoiceId) = 0;
};

struct InvoiceDeliveryResult {
    std::string status;
    std::string message;
    bool sent = false;
};

class InvoiceDeliveryService {
public:
    InvoiceDeliveryService(InvoiceDeliveryStore& store, EmailService& emailService);

    InvoiceDeliveryResult sendInvoice(const std::string& invoiceId);

private:
    InvoiceDeliveryStore& store;
    EmailService& emailService;
    InvoicePdfGenerator pdfGenerator;
};

}

#endif
