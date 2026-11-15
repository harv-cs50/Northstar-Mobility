
#include "models/Vehicle.h"
#include <stdexcept>

namespace vrms {

Vehicle::Vehicle(const std::string& id, const std::string& brand,
                 const std::string& model, double rate)
    : vehicleId(id), brand(brand), model(model),
      dailyRate(rate), available(true) {

    if (id.empty() || brand.empty() || model.empty() || rate <= 0)
        throw std::invalid_argument("Invalid vehicle details");
}

std::string Vehicle::getVehicleId() const {
    return vehicleId;
}

std::string Vehicle::getBrand() const {
    return brand;
}

std::string Vehicle::getModel() const {
    return model;
}

double Vehicle::getDailyRate() const {
    return dailyRate;
}

bool Vehicle::isAvailable() const {
    return available;
}

void Vehicle::setAvailability(bool status) {
    available = status;
}

void Vehicle::setDailyRate(double rate) {
    if (rate <= 0)
        throw std::invalid_argument("Rate must be positive");

    dailyRate = rate;
}

}
