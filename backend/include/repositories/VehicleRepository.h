
#ifndef VEHICLE_REPOSITORY_H
#define VEHICLE_REPOSITORY_H

#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace vrms {

struct VehicleRecord {
    std::string id;
    std::string brand;
    std::string model;
    std::string type;
    double dailyRate;
    bool available;
};

class VehicleRepository {
private:
    pqxx::connection connection;

public:
    VehicleRepository();

    void saveVehicle(
        const std::string& id,
        const std::string& brand,
        const std::string& model,
        const std::string& type,
        double dailyRate
    );

    std::vector<VehicleRecord> getAvailableVehicles();

    std::vector<VehicleRecord> getAllVehicles();
};

}

#endif
