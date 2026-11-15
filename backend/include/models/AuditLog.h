
#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include <string>

namespace vrms {

class AuditLog {
private:
    std::string logId;
    std::string userId;
    std::string action;
    std::string description;
    std::string timestamp;

public:
    AuditLog(
        const std::string& logId,
        const std::string& userId,
        const std::string& action,
        const std::string& description
    );

    std::string getLogId() const;
    std::string getUserId() const;
    std::string getAction() const;
    std::string getDescription() const;
    std::string getTimestamp() const;

    void displayLog() const;
};

}

#endif
