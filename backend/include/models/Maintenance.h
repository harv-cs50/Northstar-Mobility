
#ifndef MAINTENANCE_H
#define MAINTENANCE_H

#include <string>

namespace vrms {

class Maintenance {
private:
    std::string maintenanceId;
    std::string vehicleId;
    std::string serviceDate;
    std::string description;
    std::string status;
    double maintenanceCost;
    std::string completionDate;

public:
    Maintenance(
        const std::string& id,
        const std::string& vehicle,
        const std::string& date,
        const std::string& details,
        double cost
    );

    std::string getMaintenanceId() const;
    std::string getVehicleId() const;
    std::string getServiceDate() const;
    std::string getDescription() const;
    std::string getStatus() const;
    double getMaintenanceCost() const;
    std::string getCompletionDate() const;

    bool isCompleted() const;

    void completeMaintenance(const std::string& date);
};

}

#endif
