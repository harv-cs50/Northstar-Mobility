
#include "models/Invoice.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>

namespace vrms {

Invoice::Invoice(const std::string& id,
                 const std::string& booking,
                 double subtotal,
                 double discount,
                 double tax)
    : invoiceId(id),
      bookingId(booking),
      subtotal(subtotal),
      discountPercentage(discount),
      taxPercentage(tax) {

    if (id.empty() || booking.empty() || subtotal <= 0 ||
        discount < 0 || discount > 100 ||
        tax < 0 || tax > 100) {
        throw std::invalid_argument("Invalid invoice details");
    }
}

double Invoice::calculateDiscount() const {
    return subtotal * discountPercentage / 100.0;
}

double Invoice::calculateTax() const {
    return (subtotal - calculateDiscount()) * taxPercentage / 100.0;
}

double Invoice::calculateTotal() const {
    return subtotal - calculateDiscount() + calculateTax();
}

std::string Invoice::getInvoiceId() const {
    return invoiceId;
}

std::string Invoice::getBookingId() const {
    return bookingId;
}

void Invoice::displayInvoice() const {
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "\n===== INVOICE =====\n";
    std::cout << "Invoice ID: " << invoiceId << "\n";
    std::cout << "Booking ID: " << bookingId << "\n";
    std::cout << "Subtotal: Rs. " << subtotal << "\n";
    std::cout << "Discount: Rs. " << calculateDiscount() << "\n";
    std::cout << "Tax: Rs. " << calculateTax() << "\n";
    std::cout << "Total: Rs. " << calculateTotal() << "\n";
}

}
