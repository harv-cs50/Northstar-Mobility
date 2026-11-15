
#include "models/Maintenance.h"
#include "validation/MaintenanceValidation.h"

namespace vrms {

Maintenance::Maintenance(
    const std::string& id,
    const std::string& vehicle,
    const std::string& date,
    const std::string& details,
    double cost
)
    : maintenanceId(id),
      vehicleId(vehicle),
      serviceDate(date),
      description(details),
      status(maintenance_validation::ScheduledStatus),
      maintenanceCost(cost),
      completionDate("") {
    maintenance_validation::validateSchedule(
        maintenanceId,
        vehicleId,
        serviceDate,
        description,
        maintenanceCost
    );
}

std::string Maintenance::getMaintenanceId() const {
    return maintenanceId;
}

std::string Maintenance::getVehicleId() const {
    return vehicleId;
}

std::string Maintenance::getServiceDate() const {
    return serviceDate;
}

std::string Maintenance::getDescription() const {
    return description;
}

std::string Maintenance::getStatus() const {
    return status;
}

double Maintenance::getMaintenanceCost() const {
    return maintenanceCost;
}

std::string Maintenance::getCompletionDate() const {
    return completionDate;
}

bool Maintenance::isCompleted() const {
    return status == maintenance_validation::CompletedStatus;
}

void Maintenance::completeMaintenance(
    const std::string& date
) {
    maintenance_validation::validateCompletionInput(
        maintenanceId,
        date
    );
    maintenance_validation::validateStatusForCompletion(status);
    maintenance_validation::validateCompletionDate(date, serviceDate);

    completionDate = date;
    status = maintenance_validation::CompletedStatus;
}

}
