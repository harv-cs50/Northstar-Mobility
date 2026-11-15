
#include "services/SessionManager.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>

namespace vrms {

std::string SessionManager::generateSessionId() const {
    std::array<unsigned char, 32> randomBytes{};

    NTSTATUS result = BCryptGenRandom(
        nullptr,
        randomBytes.data(),
        static_cast<ULONG>(randomBytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (result != 0) {
        throw std::runtime_error("Session ID generation failed");
    }

    std::ostringstream stream;

    for (unsigned char byte : randomBytes) {
        stream << std::hex
               << std::setw(2)
               << std::setfill('0')
               << static_cast<int>(byte);
    }

    return stream.str();
}

std::string SessionManager::createSession(
    const std::string& userId,
    const std::string& role
) {
    if (userId.empty() ||
        (role != "CUSTOMER" && role != "ADMIN")) {
        throw std::invalid_argument("Invalid session details");
    }

    std::string sessionId = generateSessionId();

    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(sessionMutex);

    sessions.emplace(
        sessionId,
        Session{
            userId,
            role,
            now,
            now
        }
    );

    return sessionId;
}

bool SessionManager::isSessionValid(
    const std::string& sessionId
) {
    std::lock_guard<std::mutex> lock(sessionMutex);

    auto it = sessions.find(sessionId);

    if (it == sessions.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    if (now - it->second.lastActivity >= sessionTimeout) {
        sessions.erase(it);
        return false;
    }

    it->second.lastActivity = now;
    return true;
}

std::string SessionManager::getUserId(
    const std::string& sessionId
) {
    std::lock_guard<std::mutex> lock(sessionMutex);

    auto it = sessions.find(sessionId);

    if (it == sessions.end()) {
        return "";
    }

    auto now = std::chrono::steady_clock::now();

    if (now - it->second.lastActivity >= sessionTimeout) {
        sessions.erase(it);
        return "";
    }

    it->second.lastActivity = now;
    return it->second.userId;
}

std::string SessionManager::getUserRole(
    const std::string& sessionId
) {
    std::lock_guard<std::mutex> lock(sessionMutex);

    auto it = sessions.find(sessionId);

    if (it == sessions.end()) {
        return "";
    }

    auto now = std::chrono::steady_clock::now();

    if (now - it->second.lastActivity >= sessionTimeout) {
        sessions.erase(it);
        return "";
    }

    it->second.lastActivity = now;
    return it->second.role;
}

bool SessionManager::logout(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(sessionMutex);

    return sessions.erase(sessionId) > 0;
}

void SessionManager::removeExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessionMutex);

    auto now = std::chrono::steady_clock::now();

    for (auto it = sessions.begin(); it != sessions.end();) {
        if (now - it->second.lastActivity >= sessionTimeout) {
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t SessionManager::getActiveSessionCount() {
    removeExpiredSessions();

    std::lock_guard<std::mutex> lock(sessionMutex);

    return sessions.size();
}

}
