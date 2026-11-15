
#include "repositories/VehicleRepository.h"
#include "DatabaseConfig.h"
#include <stdexcept>

namespace vrms {

VehicleRepository::VehicleRepository()
    : connection(createDatabaseConnection()) {}

void VehicleRepository::saveVehicle(
    const std::string& id,
    const std::string& brand,
    const std::string& model,
    const std::string& type,
    double dailyRate
) {
    if (id.empty() || brand.empty() || model.empty() ||
        type.empty() || dailyRate <= 0) {
        throw std::invalid_argument("Invalid vehicle details");
    }

    pqxx::work transaction(connection);

    transaction.exec_params(
        "INSERT INTO vehicles "
        "(vehicle_id, brand, model, vehicle_type, daily_rate) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (vehicle_id) DO UPDATE SET "
        "brand = EXCLUDED.brand, "
        "model = EXCLUDED.model, "
        "vehicle_type = EXCLUDED.vehicle_type, "
        "daily_rate = EXCLUDED.daily_rate",
        id, brand, model, type, dailyRate
    );

    transaction.commit();
}

std::vector<VehicleRecord> VehicleRepository::getAvailableVehicles() {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT vehicle_id, brand, model, vehicle_type, "
        "daily_rate, is_available "
        "FROM vehicles WHERE is_available = TRUE "
        "ORDER BY vehicle_id"
    );

    std::vector<VehicleRecord> vehicles;

    for (const auto& row : result) {
        vehicles.push_back({
            row["vehicle_id"].as<std::string>(),
            row["brand"].as<std::string>(),
            row["model"].as<std::string>(),
            row["vehicle_type"].as<std::string>(),
            row["daily_rate"].as<double>(),
            row["is_available"].as<bool>()
        });
    }

    transaction.commit();

    return vehicles;
}

std::vector<VehicleRecord> VehicleRepository::getAllVehicles() {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT vehicle_id, brand, model, vehicle_type, "
        "daily_rate, is_available FROM vehicles ORDER BY vehicle_id"
    );

    std::vector<VehicleRecord> vehicles;
    for (const auto& row : result) {
        vehicles.push_back({
            row["vehicle_id"].as<std::string>(),
            row["brand"].as<std::string>(),
            row["model"].as<std::string>(),
            row["vehicle_type"].as<std::string>(),
            row["daily_rate"].as<double>(),
            row["is_available"].as<bool>()
        });
    }

    transaction.commit();
    return vehicles;
}

}
