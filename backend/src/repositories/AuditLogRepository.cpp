
#include "repositories/AuditLogRepository.h"
#include "DatabaseConfig.h"

#include <stdexcept>

namespace vrms {

AuditLogRepository::AuditLogRepository()
    : connection(createDatabaseConnection()) {}

void AuditLogRepository::saveLog(const AuditLog& log) {
    pqxx::work transaction(connection);

    transaction.exec(
        "INSERT INTO audit_logs "
        "(log_id, user_id, action, description) "
        "VALUES ($1, $2, $3, $4)",
        pqxx::params{
            log.getLogId(),
            log.getUserId(),
            log.getAction(),
            log.getDescription()
        }
    );

    transaction.commit();
}

std::vector<AuditLog> AuditLogRepository::getAllLogs() {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT log_id, user_id, action, description "
        "FROM audit_logs ORDER BY created_at DESC"
    );

    std::vector<AuditLog> logs;

    for (const auto& row : result) {
        logs.emplace_back(
            row["log_id"].as<std::string>(),
            row["user_id"].as<std::string>(),
            row["action"].as<std::string>(),
            row["description"].as<std::string>()
        );
    }

    transaction.commit();

    return logs;
}

std::vector<AuditLog> AuditLogRepository::getLogsByUser(
    const std::string& userId
) {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT log_id, user_id, action, description "
        "FROM audit_logs "
        "WHERE user_id = $1 "
        "ORDER BY created_at DESC",
        pqxx::params{userId}
    );

    std::vector<AuditLog> logs;

    for (const auto& row : result) {
        logs.emplace_back(
            row["log_id"].as<std::string>(),
            row["user_id"].as<std::string>(),
            row["action"].as<std::string>(),
            row["description"].as<std::string>()
        );
    }

    transaction.commit();

    return logs;
}

}
