
#ifndef CAR_H
#define CAR_H

#include "models/Vehicle.h"

namespace vrms {

class Car : public Vehicle {
public:
    Car(const std::string& id, const std::string& brand,
        const std::string& model, double rate);

    double calculateRent(int days) const override;
    std::string getVehicleType() const override;
};

}

#endif
