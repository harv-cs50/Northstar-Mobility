
#ifndef PAYMENT_REPOSITORY_H
#define PAYMENT_REPOSITORY_H

#include <pqxx/pqxx>
#include <optional>
#include <string>
#include <vector>

namespace vrms {

struct PaymentRecord {
    std::string paymentId;
    std::string bookingId;
    double amount;
    std::string method;
    std::string status;
};

class PaymentRepository {
private:
    pqxx::connection connection;

public:
    PaymentRepository();

    void savePayment(
        const std::string& paymentId,
        const std::string& bookingId,
        double amount,
        const std::string& method,
        const std::string& status
    );

    PaymentRecord getPaymentById(
        const std::string& paymentId
    );

    std::vector<PaymentRecord> getAllPayments();

    // Returns only payments whose booking belongs to the supplied customer.
    std::vector<PaymentRecord> getCustomerPayments(
        const std::string& customerId
    );

    std::optional<PaymentRecord> getSuccessfulPaymentForBooking(
        const std::string& bookingId
    );

    void updatePaymentStatus(
        const std::string& paymentId,
        const std::string& status
    );
};

}

#endif
