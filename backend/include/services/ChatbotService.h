#ifndef CHATBOT_SERVICE_H
#define CHATBOT_SERVICE_H

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vrms {

struct ChatVehicle {
    std::string id;
    std::string brand;
    std::string model;
    std::string type;
    double dailyRate = 0.0;
};

struct ChatBooking {
    std::string bookingId;
    std::string vehicleId;
    std::string startDate;
    std::string endDate;
    std::string status;
};

struct ChatPayment {
    std::string bookingId;
    double amount = 0.0;
    std::string status;
};

struct ChatInvoice {
    std::string invoiceId;
    std::string bookingId;
    std::string emailStatus;
};

class ChatbotDataSource {
public:
    virtual ~ChatbotDataSource() = default;
    virtual std::vector<ChatVehicle> getAvailableVehicles() = 0;
    virtual std::vector<ChatBooking> getCustomerBookings(
        const std::string& customerId) = 0;
    virtual std::vector<ChatPayment> getCustomerPayments(
        const std::string& customerId) = 0;
    virtual std::vector<ChatInvoice> getCustomerInvoices(
        const std::string& customerId) = 0;
};

class ChatbotGenerator {
public:
    virtual ~ChatbotGenerator() = default;
    virtual std::string generate(const std::string& systemInstruction,
                                 const std::string& userContent) = 0;
};

class ChatbotError : public std::runtime_error {
public:
    ChatbotError(int httpStatus, const std::string& message);
    int httpStatus() const noexcept;

private:
    int status;
};

class ChatbotService {
public:
    ChatbotService(ChatbotDataSource& dataSource, ChatbotGenerator& generator);
    ChatbotService(ChatbotDataSource& dataSource, ChatbotGenerator* generator);

    // Identity values must come from the server-authenticated session.
    std::string reply(const std::string& message,
                      const std::string& recentHistory,
                      const std::string& authenticatedUserId,
                      const std::string& authenticatedRole);

    static void validateRequest(const std::string& message,
                                const std::string& recentHistory);
    static bool isCustomerAccountQuestion(const std::string& message);
    static bool isDatabaseAnswerableQuestion(const std::string& message);

    static constexpr std::size_t MaxMessageLength = 1000;
    static constexpr std::size_t MaxHistoryLength = 4000;

private:
    ChatbotDataSource& dataSource;
    ChatbotGenerator* generator;
};

class ChatbotRateLimiter {
public:
    explicit ChatbotRateLimiter(std::size_t maxRequestsPerMinute = 12);
    bool allow(const std::string& clientKey,
               std::chrono::steady_clock::time_point now =
                   std::chrono::steady_clock::now());

private:
    std::size_t limit;
    std::mutex mutex;
    std::unordered_map<std::string,
        std::deque<std::chrono::steady_clock::time_point>> requestsByClient;
};

} // namespace vrms

#endif
