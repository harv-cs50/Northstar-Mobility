
#include "services/MaintenanceService.h"
#include "validation/MaintenanceValidation.h"

#include <exception>

namespace vrms {

namespace {

template <typename RepositoryOperation>
void delegateValidationFailure(
    RepositoryOperation&& repositoryOperation,
    std::exception_ptr validationError
) {
    repositoryOperation();
    std::rethrow_exception(validationError);
}

}

void MaintenanceService::scheduleMaintenance(
    const std::string& maintenanceId,
    const std::string& vehicleId,
    const std::string& serviceDate,
    const std::string& description,
    double maintenanceCost,
    const std::string& userId
) {
    try {
        maintenance_validation::validateSchedule(
            maintenanceId,
            vehicleId,
            serviceDate,
            description,
            maintenanceCost
        );
    }
    catch (...) {
        auto validationError = std::current_exception();
        delegateValidationFailure(
            [&] {
                maintenanceRepository.scheduleMaintenance(
                    maintenanceId,
                    vehicleId,
                    serviceDate,
                    description,
                    maintenanceCost,
                    userId
                );
            },
            validationError
        );
        return;
    }

    maintenanceRepository.scheduleMaintenance(
        maintenanceId,
        vehicleId,
        serviceDate,
        description,
        maintenanceCost,
        userId
    );
}

void MaintenanceService::completeMaintenance(
    const std::string& maintenanceId,
    const std::string& completionDate,
    const std::string& userId
) {
    try {
        maintenance_validation::validateCompletionInput(
            maintenanceId,
            completionDate
        );
    }
    catch (...) {
        auto validationError = std::current_exception();
        delegateValidationFailure(
            [&] {
                maintenanceRepository.completeMaintenance(
                    maintenanceId,
                    completionDate,
                    userId
                );
            },
            validationError
        );
        return;
    }

    MaintenanceRecord record;
    try {
        record = maintenanceRepository.getMaintenanceById(maintenanceId);
    }
    catch (...) {
        // Let the mutation path preserve its not-found/error audit behavior.
        maintenanceRepository.completeMaintenance(
            maintenanceId,
            completionDate,
            userId
        );
        return;
    }

    try {
        maintenance_validation::validateStatusForCompletion(record.status);
        maintenance_validation::validateCompletionDate(
            completionDate,
            record.serviceDate
        );
    }
    catch (...) {
        auto validationError = std::current_exception();
        delegateValidationFailure(
            [&] {
                maintenanceRepository.completeMaintenance(
                    maintenanceId,
                    completionDate,
                    userId
                );
            },
            validationError
        );
        return;
    }

    maintenanceRepository.completeMaintenance(
        maintenanceId,
        completionDate,
        userId
    );
}

std::vector<MaintenanceRecord>
MaintenanceService::getAllMaintenance() {
    return maintenanceRepository.getAllMaintenance();
}

MaintenanceRecord MaintenanceService::getMaintenanceById(
    const std::string& maintenanceId
) {
    return maintenanceRepository.getMaintenanceById(
        maintenanceId
    );
}

std::vector<MaintenanceRecord>
MaintenanceService::getVehicleMaintenance(
    const std::string& vehicleId
) {
    return maintenanceRepository.getVehicleMaintenance(
        vehicleId
    );
}

}
