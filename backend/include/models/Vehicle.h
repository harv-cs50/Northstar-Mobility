
#ifndef VEHICLE_H
#define VEHICLE_H

#include <string>

namespace vrms {

class Vehicle {
private:
    std::string vehicleId;
    std::string brand;
    std::string model;
    double dailyRate;
    bool available;

public:
    Vehicle(const std::string& id, const std::string& brand,
            const std::string& model, double rate);

    virtual ~Vehicle() = default;

    std::string getVehicleId() const;
    std::string getBrand() const;
    std::string getModel() const;
    double getDailyRate() const;
    bool isAvailable() const;

    void setAvailability(bool status);
    void setDailyRate(double rate);

    virtual double calculateRent(int days) const = 0;
    virtual std::string getVehicleType() const = 0;

};

}

#endif
