
#ifndef MAINTENANCE_REPOSITORY_H
#define MAINTENANCE_REPOSITORY_H

#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace vrms {

struct MaintenanceRecord {
    std::string maintenanceId;
    std::string vehicleId;
    std::string serviceDate;
    std::string description;
    std::string status;
    double maintenanceCost;
    std::string completionDate;
};

class MaintenanceRepository {
private:
    pqxx::connection connection;

public:
    MaintenanceRepository();

    void scheduleMaintenance(
        const std::string& maintenanceId,
        const std::string& vehicleId,
        const std::string& serviceDate,
        const std::string& description,
        double maintenanceCost,
        const std::string& userId = ""
    );

    void completeMaintenance(
        const std::string& maintenanceId,
        const std::string& completionDate,
        const std::string& userId = ""
    );

    std::vector<MaintenanceRecord> getAllMaintenance();

    MaintenanceRecord getMaintenanceById(
        const std::string& maintenanceId
    );

    std::vector<MaintenanceRecord> getVehicleMaintenance(
        const std::string& vehicleId
    );
};

}

#endif
