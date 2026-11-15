
#ifndef BIKE_H
#define BIKE_H

#include "models/Vehicle.h"

namespace vrms {

class Bike : public Vehicle {
public:
    Bike(const std::string& id, const std::string& brand,
         const std::string& model, double rate);

    double calculateRent(int days) const override;
    std::string getVehicleType() const override;
};

}

#endif
