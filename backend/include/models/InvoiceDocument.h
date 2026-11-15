#ifndef INVOICE_DOCUMENT_H
#define INVOICE_DOCUMENT_H

#include <string>

namespace vrms {

struct InvoiceDocumentData {
    std::string invoiceId;
    std::string bookingId;
    std::string customerId;
    std::string customerName;
    std::string customerEmail;
    std::string vehicleId;
    std::string vehicleBrand;
    std::string vehicleModel;
    std::string vehicleType;
    std::string rentalStartDate;
    std::string rentalEndDate;
    int durationDays = 0;
    double dailyRate = 0.0;
    double subtotal = 0.0;
    double discount = 0.0;
    double tax = 0.0;
    double total = 0.0;
    std::string paymentStatus;
};

}

#endif
