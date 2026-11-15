
#ifndef INVOICE_SERVICE_H
#define INVOICE_SERVICE_H

#include "models/Invoice.h"
#include "repositories/InvoiceRepository.h"
#include "services/InvoiceDeliveryService.h"

#include <string>
#include <vector>

namespace vrms {

class InvoiceService : public InvoiceDeliveryStore {
private:
    InvoiceRepository repository;

public:
    void createInvoice(const Invoice& invoice);

    InvoiceCreateResult createOrGetInvoice(const Invoice& invoice);

    InvoiceRecord getInvoiceById(
        const std::string& invoiceId
    );

    std::vector<InvoiceRecord> getAllInvoices();

    InvoiceDocumentData loadInvoiceDocument(const std::string& invoiceId) override;
    bool claimInvoiceEmail(const std::string& invoiceId) override;
    void markInvoiceEmailSent(const std::string& invoiceId) override;
    void markInvoiceEmailFailed(const std::string& invoiceId) override;
    std::string getInvoiceEmailStatus(const std::string& invoiceId) override;
};

}

#endif
