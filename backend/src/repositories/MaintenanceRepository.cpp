
#include "repositories/MaintenanceRepository.h"
#include "repositories/AuditLogRepository.h"
#include "models/AuditLog.h"
#include "validation/MaintenanceValidation.h"
#include "DatabaseConfig.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace vrms {

namespace {

void recordMaintenanceAudit(
    const std::string& action,
    const std::string& maintenanceId,
    const std::string& vehicleId,
    const std::string& userId,
    const std::string& details = ""
) {
    AuditLogRepository auditRepository;

    std::string logId =
        "LOG_MAINT_" +
        std::to_string(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count()
        );

    std::string description =
        "Maintenance ID: " + maintenanceId +
        ", Vehicle ID: " + vehicleId;

    if (!details.empty()) {
        description += ", Details: " + details;
    }

    AuditLog log(
        logId,
        userId,
        action,
        description
    );

    auditRepository.saveLog(log);

    std::cout << "[AUDIT] " << action
              << " recorded successfully.\n";
}

template <typename Row>
MaintenanceRecord convertRow(const Row& row) {
    MaintenanceRecord record;

    record.maintenanceId =
        row["maintenance_id"].template as<std::string>();

    record.vehicleId =
        row["vehicle_id"].template as<std::string>();

    record.serviceDate =
        row["service_date"].template as<std::string>();

    record.description =
        row["description"].template as<std::string>();

    record.status =
        row["status"].template as<std::string>();

    record.maintenanceCost =
        row["maintenance_cost"].template as<double>();

    record.completionDate =
        row["completion_date"].is_null()
            ? ""
            : row["completion_date"].template as<std::string>();

    return record;
}

}

MaintenanceRepository::MaintenanceRepository()
    : connection(createDatabaseConnection()) {
    if (!connection.is_open()) {
        throw std::runtime_error(
            "Failed to connect to maintenance database"
        );
    }
}

void MaintenanceRepository::scheduleMaintenance(
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

        pqxx::work transaction(connection);

        auto vehicle = transaction.exec_params(
            "SELECT vehicle_id, is_available FROM vehicles "
            "WHERE vehicle_id = $1 FOR UPDATE",
            vehicleId
        );

        if (vehicle.empty()) {
            throw std::runtime_error("Vehicle not found");
        }

        if (!vehicle[0]["is_available"].as<bool>()) {
            throw std::runtime_error(
                "Vehicle is currently unavailable for maintenance scheduling"
            );
        }

        transaction.exec_params(
            "INSERT INTO maintenance_records "
            "(maintenance_id, vehicle_id, service_date, description, "
            "status, maintenance_cost, completion_date) "
            "VALUES ($1, $2, $3, $4, $5, $6, NULL)",
            maintenanceId,
            vehicleId,
            serviceDate,
            description,
            std::string(maintenance_validation::ScheduledStatus),
            maintenanceCost
        );

        transaction.exec_params(
            "UPDATE vehicles SET is_available = FALSE "
            "WHERE vehicle_id = $1",
            vehicleId
        );

        transaction.commit();
    }
    catch (const std::exception& operationError) {
        try {
            recordMaintenanceAudit(
                "MAINTENANCE_SCHEDULE_FAILED",
                maintenanceId,
                vehicleId,
                userId,
                operationError.what()
            );
        }
        catch (const std::exception& auditError) {
            throw std::runtime_error(
                std::string("Maintenance scheduling failed: ") +
                operationError.what() +
                "; failure audit logging also failed: " +
                auditError.what()
            );
        }
        catch (...) {
            throw std::runtime_error(
                std::string("Maintenance scheduling failed: ") +
                operationError.what() +
                "; failure audit logging also failed with an unknown error"
            );
        }

        throw;
    }

    try {
        recordMaintenanceAudit(
            "MAINTENANCE_SCHEDULED",
            maintenanceId,
            vehicleId,
            userId
        );
    }
    catch (const std::exception& auditError) {
        throw std::runtime_error(
            std::string("Maintenance was scheduled, but success audit logging failed: ") +
            auditError.what()
        );
    }
    catch (...) {
        throw std::runtime_error(
            "Maintenance was scheduled, but success audit logging failed "
            "with an unknown error"
        );
    }
}

void MaintenanceRepository::completeMaintenance(
    const std::string& maintenanceId,
    const std::string& completionDate,
    const std::string& userId
) {
    std::string vehicleId;

    try {
        maintenance_validation::validateCompletionInput(
            maintenanceId,
            completionDate
        );

        pqxx::work transaction(connection);

        auto result = transaction.exec_params(
            "SELECT vehicle_id, service_date "
            "FROM maintenance_records "
            "WHERE maintenance_id = $1 AND status = $2",
            maintenanceId,
            std::string(maintenance_validation::ScheduledStatus)
        );

        if (result.empty()) {
            auto existing = transaction.exec_params(
                "SELECT status FROM maintenance_records "
                "WHERE maintenance_id = $1",
                maintenanceId
            );

            if (existing.empty()) {
                throw std::runtime_error(
                    "Maintenance record not found"
                );
            }

            maintenance_validation::validateStatusForCompletion(
                existing[0]["status"].as<std::string>()
            );
        }

        // Retrieve the vehicle ID from the maintenance record.
        vehicleId =
            result[0]["vehicle_id"].as<std::string>();

        std::string serviceDate =
            result[0]["service_date"].as<std::string>();

        maintenance_validation::validateCompletionDate(
            completionDate,
            serviceDate
        );

        auto maintenanceUpdateResult = transaction.exec_params(
            "UPDATE maintenance_records "
            "SET status = $1, completion_date = $2 "
            "WHERE maintenance_id = $3 AND status = $4",
            std::string(maintenance_validation::CompletedStatus),
            completionDate,
            maintenanceId,
            std::string(maintenance_validation::ScheduledStatus)
        );

        if (maintenanceUpdateResult.affected_rows() != 1) {
            throw std::runtime_error(
                "Maintenance is already completed or cannot be updated"
            );
        }

        auto availabilityUpdateResult = transaction.exec_params(
            "UPDATE vehicles SET is_available = TRUE "
            "WHERE vehicle_id = $1 "
            "AND NOT EXISTS ("
            "SELECT 1 FROM maintenance_records "
            "WHERE vehicle_id = $1 AND status = $2"
            ")",
            vehicleId,
            std::string(maintenance_validation::ScheduledStatus)
        );

        std::cout << "Vehicle availability rows updated: "
                  << availabilityUpdateResult.affected_rows()
                  << '\n';

        transaction.commit();
    }
    catch (const std::exception& operationError) {
        try {
            recordMaintenanceAudit(
                "MAINTENANCE_COMPLETION_FAILED",
                maintenanceId,
                vehicleId,
                userId,
                operationError.what()
            );
        }
        catch (const std::exception& auditError) {
            throw std::runtime_error(
                std::string("Maintenance completion failed: ") +
                operationError.what() +
                "; failure audit logging also failed: " +
                auditError.what()
            );
        }
        catch (...) {
            throw std::runtime_error(
                std::string("Maintenance completion failed: ") +
                operationError.what() +
                "; failure audit logging also failed with an unknown error"
            );
        }

        throw;
    }

    try {
        recordMaintenanceAudit(
            "MAINTENANCE_COMPLETED",
            maintenanceId,
            vehicleId,
            userId
        );
    }
    catch (const std::exception& auditError) {
        throw std::runtime_error(
            std::string("Maintenance was completed, but success audit logging failed: ") +
            auditError.what()
        );
    }
    catch (...) {
        throw std::runtime_error(
            "Maintenance was completed, but success audit logging failed "
            "with an unknown error"
        );
    }
}

std::vector<MaintenanceRecord>
MaintenanceRepository::getAllMaintenance() {
    pqxx::read_transaction transaction(connection);

    auto result = transaction.exec(
        "SELECT * FROM maintenance_records "
        "ORDER BY service_date DESC"
    );

    std::vector<MaintenanceRecord> records;

    for (const auto& row : result) {
        records.push_back(convertRow(row));
    }

    return records;
}

MaintenanceRecord MaintenanceRepository::getMaintenanceById(
    const std::string& maintenanceId
) {
    pqxx::read_transaction transaction(connection);

    auto result = transaction.exec_params(
        "SELECT * FROM maintenance_records "
        "WHERE maintenance_id = $1",
        maintenanceId
    );

    if (result.empty()) {
        throw std::runtime_error(
            "Maintenance record not found"
        );
    }

    return convertRow(result[0]);
}

std::vector<MaintenanceRecord>
MaintenanceRepository::getVehicleMaintenance(
    const std::string& vehicleId
) {
    pqxx::read_transaction transaction(connection);

    auto result = transaction.exec_params(
        "SELECT * FROM maintenance_records "
        "WHERE vehicle_id = $1 "
        "ORDER BY service_date DESC",
        vehicleId
    );

    std::vector<MaintenanceRecord> records;

    for (const auto& row : result) {
        records.push_back(convertRow(row));
    }

    return records;
}

}
