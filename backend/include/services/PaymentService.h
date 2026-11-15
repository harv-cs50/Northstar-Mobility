
#ifndef PAYMENT_SERVICE_H
#define PAYMENT_SERVICE_H

#include "models/Payment.h"
#include "repositories/PaymentRepository.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vrms {

class PaymentService {
private:
    PaymentRepository repository;

public:
    bool processPayment(Payment& payment);

    PaymentRecord getPaymentById(
        const std::string& paymentId
    );

    std::vector<PaymentRecord> getAllPayments();

    std::optional<PaymentRecord> getSuccessfulPaymentForBooking(
        const std::string& bookingId
    );
};

}

#endif
