#ifndef POSTGRES_CHATBOT_DATA_SOURCE_H
#define POSTGRES_CHATBOT_DATA_SOURCE_H

#include "services/ChatbotService.h"

namespace vrms {

class PostgresChatbotDataSource final : public ChatbotDataSource {
public:
    std::vector<ChatVehicle> getAvailableVehicles() override;
    std::vector<ChatBooking> getCustomerBookings(
        const std::string& customerId) override;
    std::vector<ChatPayment> getCustomerPayments(
        const std::string& customerId) override;
    std::vector<ChatInvoice> getCustomerInvoices(
        const std::string& customerId) override;
};

} // namespace vrms

#endif
