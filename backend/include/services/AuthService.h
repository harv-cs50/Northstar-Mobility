
#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <string>

#include "services/SessionManager.h"

namespace vrms {

struct AuthResult {
    bool success;
    std::string userId;
    std::string name;
    std::string role;
    std::string message;
    std::string sessionId = "";
};

class AuthService {
private:
    SessionManager sessionManager;

public:
    AuthResult registerCustomer(
        const std::string& userId,
        const std::string& name,
        const std::string& email,
        const std::string& password,
        const std::string& membershipLevel = "Regular"
    );

    AuthResult registerAdmin(
        const std::string& userId,
        const std::string& name,
        const std::string& email,
        const std::string& password,
        const std::string& actingSessionId
    );

    AuthResult registerInitialAdmin(
        const std::string& userId,
        const std::string& name,
        const std::string& email,
        const std::string& password,
        const std::string& bootstrapToken
    );

    AuthResult login(
        const std::string& email,
        const std::string& password
    );

    bool logout(const std::string& sessionId);

    bool isSessionValid(const std::string& sessionId);

    std::string getSessionUserId(
        const std::string& sessionId
    );

    std::string getSessionUserRole(
        const std::string& sessionId
    );
};

}

#endif
