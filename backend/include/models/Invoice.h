
#ifndef INVOICE_H
#define INVOICE_H

#include <string>

namespace vrms {

class Invoice {
private:
    std::string invoiceId;
    std::string bookingId;
    double subtotal;
    double discountPercentage;
    double taxPercentage;

public:
    Invoice(const std::string& id,
            const std::string& booking,
            double subtotal,
            double discount,
            double tax);

    double calculateDiscount() const;
    double calculateTax() const;
    double calculateTotal() const;

    std::string getInvoiceId() const;
    std::string getBookingId() const;

    void displayInvoice() const;
};

}

#endif
