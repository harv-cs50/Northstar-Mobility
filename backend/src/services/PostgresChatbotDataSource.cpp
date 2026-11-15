#include "services/PostgresChatbotDataSource.h"

#include "repositories/BookingRepository.h"
#include "repositories/InvoiceRepository.h"
#include "repositories/PaymentRepository.h"
#include "repositories/VehicleRepository.h"

namespace vrms {

std::vector<ChatVehicle> PostgresChatbotDataSource::getAvailableVehicles() {
    VehicleRepository repository;
    const auto records = repository.getAvailableVehicles();
    std::vector<ChatVehicle> vehicles;
    vehicles.reserve(records.size());
    for (const auto& record : records) {
        if (!record.available) continue;
        vehicles.push_back({record.id, record.brand, record.model,
                            record.type, record.dailyRate});
    }
    return vehicles;
}

std::vector<ChatBooking> PostgresChatbotDataSource::getCustomerBookings(
    const std::string& customerId) {
    BookingRepository repository;
    const auto records = repository.getCustomerBookings(customerId);
    std::vector<ChatBooking> bookings;
    bookings.reserve(records.size());
    for (const auto& record : records) {
        bookings.push_back({record.bookingId, record.vehicleId, record.startDate,
                            record.endDate, record.status});
    }
    return bookings;
}

std::vector<ChatPayment> PostgresChatbotDataSource::getCustomerPayments(
    const std::string& customerId) {
    PaymentRepository repository;
    const auto records = repository.getCustomerPayments(customerId);
    std::vector<ChatPayment> payments;
    payments.reserve(records.size());
    for (const auto& record : records)
        payments.push_back({record.bookingId, record.amount, record.status});
    return payments;
}

std::vector<ChatInvoice> PostgresChatbotDataSource::getCustomerInvoices(
    const std::string& customerId) {
    InvoiceRepository repository;
    const auto records = repository.getCustomerInvoices(customerId);
    std::vector<ChatInvoice> invoices;
    invoices.reserve(records.size());
    for (const auto& record : records)
        invoices.push_back({record.invoiceId, record.bookingId, record.emailStatus});
    return invoices;
}

} // namespace vrms
