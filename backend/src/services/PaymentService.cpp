
#include "services/PaymentService.h"

#include <stdexcept>

namespace vrms {

bool PaymentService::processPayment(Payment& payment) {

    bool successful = payment.makePayment();

    repository.savePayment(
        payment.getPaymentId(),
        payment.getBookingId(),
        payment.getAmount(),
        payment.getMethod(),
        payment.getStatusString()
    );

    return successful;
}

PaymentRecord PaymentService::getPaymentById(
    const std::string& paymentId
) {
    return repository.getPaymentById(paymentId);
}

std::vector<PaymentRecord> PaymentService::getAllPayments() {
    return repository.getAllPayments();
}

std::optional<PaymentRecord> PaymentService::getSuccessfulPaymentForBooking(
    const std::string& bookingId
) {
    return repository.getSuccessfulPaymentForBooking(bookingId);
}

}
