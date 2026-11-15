
#include "models/Bike.h"
#include <stdexcept>

namespace vrms {

Bike::Bike(const std::string& id, const std::string& brand,
           const std::string& model, double rate)
    : Vehicle(id, brand, model, rate) {}

double Bike::calculateRent(int days) const {
    if (days <= 0)
        throw std::invalid_argument("Days must be positive");

    return getDailyRate() * days;
}

std::string Bike::getVehicleType() const {
    return "Bike";
}

}
