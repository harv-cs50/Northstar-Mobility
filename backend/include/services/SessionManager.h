
#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

namespace vrms {

class SessionManager {
private:
    struct Session {
        std::string userId;
        std::string role;
        std::chrono::steady_clock::time_point createdAt;
        std::chrono::steady_clock::time_point lastActivity;
    };

    std::unordered_map<std::string, Session> sessions;
    std::mutex sessionMutex;

    std::chrono::minutes sessionTimeout{30};

    std::string generateSessionId() const;

public:
    std::string createSession(
        const std::string& userId,
        const std::string& role
    );

    bool isSessionValid(const std::string& sessionId);

    std::string getUserId(
        const std::string& sessionId
    );

    std::string getUserRole(
        const std::string& sessionId
    );

    bool logout(const std::string& sessionId);

    void removeExpiredSessions();

    std::size_t getActiveSessionCount();
};

}

#endif
