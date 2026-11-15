
#ifndef PAYMENT_H
#define PAYMENT_H

#include <string>
#include <memory>

namespace vrms {

enum class PaymentStatus {
    Pending,
    Successful,
    Failed
};

class PaymentStrategy {
public:
    virtual ~PaymentStrategy() = default;
    virtual bool processPayment(double amount) = 0;
    virtual std::string getMethodName() const = 0;
};

class UpiPayment : public PaymentStrategy {
public:
    bool processPayment(double amount) override;
    std::string getMethodName() const override;
};

class CardPayment : public PaymentStrategy {
public:
    bool processPayment(double amount) override;
    std::string getMethodName() const override;
};

class CashPayment : public PaymentStrategy {
public:
    bool processPayment(double amount) override;
    std::string getMethodName() const override;
};

class Payment {
private:
    std::string paymentId;
    std::string bookingId;
    double amount;
    PaymentStatus status;
    std::unique_ptr<PaymentStrategy> strategy;

public:
    Payment(const std::string& id,
            const std::string& booking,
            double amount,
            std::unique_ptr<PaymentStrategy> strategy);

    bool makePayment();

    std::string getPaymentId() const;
    std::string getBookingId() const;
    double getAmount() const;
    PaymentStatus getStatus() const;
    std::string getMethod() const;
    std::string getStatusString() const;
};

}

#endif
