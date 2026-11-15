
#include "models/Payment.h"
#include <stdexcept>

namespace vrms {

bool UpiPayment::processPayment(double amount) {
    return amount > 0;
}

std::string UpiPayment::getMethodName() const {
    return "UPI";
}

bool CardPayment::processPayment(double amount) {
    return amount > 0;
}

std::string CardPayment::getMethodName() const {
    return "Card";
}

bool CashPayment::processPayment(double amount) {
    return amount > 0;
}

std::string CashPayment::getMethodName() const {
    return "Cash";
}

Payment::Payment(const std::string& id,
                 const std::string& booking,
                 double amount,
                 std::unique_ptr<PaymentStrategy> strategy)
    : paymentId(id),
      bookingId(booking),
      amount(amount),
      status(PaymentStatus::Pending),
      strategy(std::move(strategy)) {

    if (id.empty() || booking.empty() || amount <= 0 || !this->strategy)
        throw std::invalid_argument("Invalid payment details");
}

bool Payment::makePayment() {
    if (status != PaymentStatus::Pending)
        throw std::logic_error("Payment has already been processed");

    status = strategy->processPayment(amount)
             ? PaymentStatus::Successful
             : PaymentStatus::Failed;

    return status == PaymentStatus::Successful;
}

std::string Payment::getPaymentId() const {
    return paymentId;
}

std::string Payment::getBookingId() const {
    return bookingId;
}

double Payment::getAmount() const {
    return amount;
}

PaymentStatus Payment::getStatus() const {
    return status;
}

std::string Payment::getMethod() const {
    return strategy->getMethodName();
}

std::string Payment::getStatusString() const {
    switch (status) {
        case PaymentStatus::Pending: return "Pending";
        case PaymentStatus::Successful: return "Successful";
        case PaymentStatus::Failed: return "Failed";
    }

    return "Unknown";
}

}
