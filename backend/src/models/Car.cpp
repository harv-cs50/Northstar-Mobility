
#include "models/Car.h"
#include <stdexcept>

namespace vrms {

Car::Car(const std::string& id, const std::string& brand,
         const std::string& model, double rate)
    : Vehicle(id, brand, model, rate) {}

double Car::calculateRent(int days) const {
    if (days <= 0)
        throw std::invalid_argument("Days must be positive");

    return getDailyRate() * days;
}

std::string Car::getVehicleType() const {
    return "Car";
}

}
