
#include "Database.h"
#include "DatabaseConfig.h"
#include <iostream>
#include <stdexcept>

namespace vrms {

Database::Database()
    : connection(createDatabaseConnection()) {}

bool Database::isConnected() const {
    return connection.is_open();
}

void Database::testConnection() {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT current_database()"
    );

    std::cout << "\n===== DATABASE CONNECTION =====\n";
    std::cout << "Database: "
              << result[0][0].as<std::string>() << "\n";
    std::cout << "Connection: Successful\n";

    transaction.commit();
}

}
