#ifndef INVOICE_PDF_GENERATOR_H
#define INVOICE_PDF_GENERATOR_H

#include "models/InvoiceDocument.h"

#include <cstdint>
#include <vector>

namespace vrms {

class InvoicePdfGenerator {
public:
    std::vector<std::uint8_t> generate(
        const InvoiceDocumentData& invoice
    ) const;
};

}

#endif
