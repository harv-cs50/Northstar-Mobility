#ifndef DATABASE_CONFIG_H
#define DATABASE_CONFIG_H

#include <pqxx/pqxx>

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace vrms {

inline pqxx::connection createDatabaseConnection() {
    constexpr const char* requiredVariables[] = {
        "PGHOST",
        "PGPORT",
        "PGDATABASE",
        "PGUSER",
        "PGPASSWORD"
    };

    for (const char* variableName : requiredVariables) {
        const char* value = std::getenv(variableName);
        if (value == nullptr || value[0] == '\0') {
            throw std::runtime_error(
                std::string("Missing required database environment variable: ") +
                variableName +
                ". Set PGHOST, PGPORT, PGDATABASE, PGUSER, and PGPASSWORD "
                "before starting VehicleRentalSystem."
            );
        }
    }

    // libpq reads the PG* variables directly from the current process environment.
    return pqxx::connection{};
}

}

#endif
