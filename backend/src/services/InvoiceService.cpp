
#include "services/InvoiceService.h"

namespace vrms {

void InvoiceService::createInvoice(const Invoice& invoice) {
    (void)repository.createOrGetInvoice(invoice);
}

InvoiceCreateResult InvoiceService::createOrGetInvoice(const Invoice& invoice) {
    return repository.createOrGetInvoice(invoice);
}

InvoiceRecord InvoiceService::getInvoiceById(
    const std::string& invoiceId
) {
    return repository.getInvoiceById(invoiceId);
}

std::vector<InvoiceRecord> InvoiceService::getAllInvoices() {
    return repository.getAllInvoices();
}

InvoiceDocumentData InvoiceService::loadInvoiceDocument(
    const std::string& invoiceId
) {
    return repository.getInvoiceDocument(invoiceId);
}

bool InvoiceService::claimInvoiceEmail(const std::string& invoiceId) {
    return repository.claimInvoiceEmail(invoiceId);
}

void InvoiceService::markInvoiceEmailSent(const std::string& invoiceId) {
    repository.markInvoiceEmailSent(invoiceId);
}

void InvoiceService::markInvoiceEmailFailed(const std::string& invoiceId) {
    repository.markInvoiceEmailFailed(invoiceId);
}

std::string InvoiceService::getInvoiceEmailStatus(const std::string& invoiceId) {
    return repository.getInvoiceEmailStatus(invoiceId);
}

}
