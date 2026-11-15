
#include "models/AuditLog.h"

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vrms {

AuditLog::AuditLog(
    const std::string& logId,
    const std::string& userId,
    const std::string& action,
    const std::string& description
)
    : logId(logId),
      userId(userId),
      action(action),
      description(description) {

    // An empty user ID represents an operation without authenticated context.
    if (logId.empty() || action.empty() || description.empty()) {
        throw std::invalid_argument("Audit log fields cannot be empty");
    }

    auto now = std::chrono::system_clock::now();
    std::time_t currentTime =
        std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};

#ifdef _WIN32
    localtime_s(&localTime, &currentTime);
#else
    localtime_r(&currentTime, &localTime);
#endif

    std::ostringstream timestampStream;

    timestampStream << std::put_time(
        &localTime, "%Y-%m-%d %H:%M:%S"
    );

    timestamp = timestampStream.str();
}

std::string AuditLog::getLogId() const {
    return logId;
}

std::string AuditLog::getUserId() const {
    return userId;
}

std::string AuditLog::getAction() const {
    return action;
}

std::string AuditLog::getDescription() const {
    return description;
}

std::string AuditLog::getTimestamp() const {
    return timestamp;
}

void AuditLog::displayLog() const {
    std::cout << "\n===== AUDIT LOG =====\n";
    std::cout << "Log ID: " << logId << "\n";
    std::cout << "User ID: " << userId << "\n";
    std::cout << "Action: " << action << "\n";
    std::cout << "Description: " << description << "\n";
    std::cout << "Timestamp: " << timestamp << "\n";
}

}
