
#ifndef MAINTENANCE_SERVICE_H
#define MAINTENANCE_SERVICE_H

#include "repositories/MaintenanceRepository.h"

#include <string>
#include <vector>

namespace vrms {

class MaintenanceService {
private:
    MaintenanceRepository maintenanceRepository;

public:
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
