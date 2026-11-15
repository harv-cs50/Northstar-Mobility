
#ifndef AUDIT_LOG_REPOSITORY_H
#define AUDIT_LOG_REPOSITORY_H

#include "models/AuditLog.h"

#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace vrms {

class AuditLogRepository {
private:
    pqxx::connection connection;

public:
    AuditLogRepository();

    void saveLog(const AuditLog& log);

    std::vector<AuditLog> getAllLogs();

    std::vector<AuditLog> getLogsByUser(
        const std::string& userId
    );
};

}

#endif
