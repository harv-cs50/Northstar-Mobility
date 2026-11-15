
#include "services/AuthService.h"
#include "services/AuthorizationService.h"
#include "DatabaseConfig.h"
#include "auth/PasswordHasher.h"
#include "repositories/AuditLogRepository.h"
#include "models/AuditLog.h"

#include <pqxx/pqxx>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace vrms {

namespace {

pqxx::connection createConnection() {
    return createDatabaseConnection();
}


std::string generateAuditId() {
    auto now = std::chrono::system_clock::now();
    auto milliseconds = std::chrono::duration_cast<
        std::chrono::milliseconds
    >(now.time_since_epoch()).count();

    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 9999);

    std::ostringstream id;
    id << "AUD" << milliseconds
       << std::setw(4) << std::setfill('0')
       << distribution(generator);

    return id.str();
}

void writeAuditLog(
    const std::string& userId,
    const std::string& action,
    const std::string& description
) noexcept {
    try {
        AuditLogRepository repository;
        AuditLog log(
            generateAuditId(),
            userId.empty() ? "UNKNOWN" : userId,
            action,
            description
        );
        repository.saveLog(log);
    } catch (...) {
        // Audit storage failure must not expose credentials
        // or silently turn a successful authentication into a failure.
    }
}

bool validEmail(const std::string& email) {
    auto at = email.find('@');
    auto dot = email.find(
        '.', at == std::string::npos ? 0 : at
    );

    return at != std::string::npos &&
           dot != std::string::npos &&
           at > 0 &&
           dot > at + 1 &&
           dot < email.size() - 1;
}

bool validPassword(const std::string& password) {
    return password.size() >= 8;
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
    const std::size_t maxLength = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t i = 0; i < maxLength; ++i) {
        const unsigned char a = i < left.size()
            ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size()
            ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<std::size_t>(a ^ b);
    }
    return difference == 0;
}

class DatabaseAdminPrincipal final : public User {
public:
    DatabaseAdminPrincipal(
        const std::string& id,
        const std::string& name,
        const std::string& email
    ) : User(id, name, email) {}

    std::string getRole() const override { return "ADMIN"; }
};

}

AuthResult AuthService::registerCustomer(
    const std::string& userId,
    const std::string& name,
    const std::string& email,
    const std::string& password,
    const std::string& membershipLevel
) {
    try {
        if (userId.empty() || name.empty() ||
            !validEmail(email) || !validPassword(password) ||
            membershipLevel.empty()) {
            writeAuditLog(userId, "REGISTRATION_FAILED",
                          "Customer registration rejected due to invalid details");
            return {false, "", "", "", "Invalid registration details"};
        }

        auto connection = createConnection();
        pqxx::work transaction(connection);

        auto existing = transaction.exec(
            "SELECT user_id FROM users "
            "WHERE user_id = $1 OR email = $2",
            pqxx::params{userId, email}
        );

        if (!existing.empty()) {
            writeAuditLog(userId, "REGISTRATION_FAILED",
                          "Customer registration rejected because user ID or email already exists");
            return {false, "", "", "", "User ID or email already exists"};
        }

        std::string passwordHash =
            PasswordHasher::hashPassword(password);

        transaction.exec(
            "INSERT INTO users "
            "(user_id, name, email, password_hash, role, membership_level) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            pqxx::params{
                userId,
                name,
                email,
                passwordHash,
                std::string("CUSTOMER"),
                membershipLevel
            }
        );

        transaction.commit();

        writeAuditLog(userId, "CUSTOMER_REGISTERED",
                      "Customer account registered successfully");

        return {
            true,
            userId,
            name,
            "CUSTOMER",
            "Customer registered successfully"
        };

    } catch (const std::exception& e) {
        writeAuditLog(userId, "REGISTRATION_FAILED",
                      "Customer registration failed due to a system error");
        return {false, "", "", "", e.what()};
    }
}

AuthResult AuthService::registerAdmin(
    const std::string& userId,
    const std::string& name,
    const std::string& email,
    const std::string& password,
    const std::string& actingSessionId
) {
    const std::string actorId = sessionManager.getUserId(actingSessionId);
    const std::string sessionRole = sessionManager.getUserRole(actingSessionId);
    if (actorId.empty() || sessionRole != "ADMIN") {
        writeAuditLog(actorId, "ADMIN_REGISTRATION_FAILED",
                      "Admin provisioning rejected without an administrator session");
        return {false, "", "", "", "Administrator authorization required"};
    }

    try {
        if (userId.empty() || name.empty() ||
            !validEmail(email) || !validPassword(password)) {
            writeAuditLog(actorId, "ADMIN_REGISTRATION_FAILED",
                          "Admin registration rejected due to invalid details");
            return {false, "", "", "", "Invalid admin details"};
        }

        auto connection = createConnection();
        pqxx::work transaction(connection);

        const auto actor = transaction.exec(
            "SELECT user_id, name, email, role FROM users "
            "WHERE user_id = $1 FOR SHARE",
            pqxx::params{actorId}
        );
        if (actor.empty() || actor[0]["role"].as<std::string>() != "ADMIN") {
            writeAuditLog(actorId, "ADMIN_REGISTRATION_FAILED",
                          "Admin provisioning rejected because the session is no longer authorized");
            return {
                false, "", "", "",
                "Administrator authorization required"
            };
        }

        DatabaseAdminPrincipal principal(
            actor[0]["user_id"].as<std::string>(),
            actor[0]["name"].as<std::string>(),
            actor[0]["email"].as<std::string>()
        );
        AuthorizationService authorization;
        if (!authorization.canProvisionAdmins(principal)) {
            return {false, "", "", "", "Administrator authorization required"};
        }

        auto existingUser = transaction.exec(
            "SELECT user_id FROM users "
            "WHERE user_id = $1 OR email = $2",
            pqxx::params{userId, email}
        );

        if (!existingUser.empty()) {
            writeAuditLog(actorId, "ADMIN_REGISTRATION_FAILED",
                          "Admin registration rejected because user ID or email already exists");
            return {false, "", "", "", "User ID or email already exists"};
        }

        std::string passwordHash =
            PasswordHasher::hashPassword(password);

        transaction.exec(
            "INSERT INTO users "
            "(user_id, name, email, password_hash, role, membership_level) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            pqxx::params{
                userId,
                name,
                email,
                passwordHash,
                std::string("ADMIN"),
                std::string("Regular")
            }
        );

        transaction.commit();

        writeAuditLog(actorId, "ADMIN_REGISTERED",
                      "Administrator account provisioned successfully");

        return {
            true,
            userId,
            name,
            "ADMIN",
            "Administrator registered successfully"
        };

    } catch (const std::exception& e) {
        writeAuditLog(actorId, "ADMIN_REGISTRATION_FAILED",
                      "Admin registration failed due to a system error");
        return {false, "", "", "", "Admin registration could not be completed"};
    }
}

AuthResult AuthService::registerInitialAdmin(
    const std::string& userId,
    const std::string& name,
    const std::string& email,
    const std::string& password,
    const std::string& bootstrapToken
) {
    const char* configuredToken = std::getenv("VRMS_ADMIN_BOOTSTRAP_TOKEN");
    if (configuredToken == nullptr || configuredToken[0] == '\0' ||
        bootstrapToken.empty() ||
        !constantTimeEqual(bootstrapToken, configuredToken)) {
        writeAuditLog("", "ADMIN_BOOTSTRAP_FAILED",
                      "Initial admin provisioning rejected by bootstrap authorization");
        return {false, "", "", "", "Initial admin provisioning is not authorized"};
    }

    if (userId.empty() || name.empty() || !validEmail(email) ||
        !validPassword(password)) {
        writeAuditLog("", "ADMIN_BOOTSTRAP_FAILED",
                      "Initial admin provisioning rejected due to invalid details");
        return {false, "", "", "", "Invalid admin details"};
    }

    try {
        const std::string passwordHash = PasswordHasher::hashPassword(password);
        auto connection = createConnection();
        pqxx::work transaction(connection);

        // All bootstrap requests serialize here. After the first transaction
        // commits, later requests observe the ADMIN row and are rejected.
        transaction.exec(
            "SELECT pg_advisory_xact_lock(1448230227, 1229863252)"
        );

        const auto admins = transaction.exec(
            "SELECT user_id FROM users WHERE role = 'ADMIN' LIMIT 1"
        );
        if (!admins.empty()) {
            writeAuditLog("", "ADMIN_BOOTSTRAP_FAILED",
                          "Initial admin provisioning rejected because an admin already exists");
            return {false, "", "", "", "Initial admin has already been provisioned"};
        }

        const auto existingUser = transaction.exec(
            "SELECT user_id FROM users WHERE user_id = $1 OR email = $2",
            pqxx::params{userId, email}
        );
        if (!existingUser.empty()) {
            return {false, "", "", "", "User ID or email already exists"};
        }

        transaction.exec(
            "INSERT INTO users "
            "(user_id, name, email, password_hash, role, membership_level) "
            "VALUES ($1, $2, $3, $4, 'ADMIN', 'Regular')",
            pqxx::params{userId, name, email, passwordHash}
        );
        transaction.commit();

        const std::string sessionId = sessionManager.createSession(userId, "ADMIN");
        writeAuditLog(userId, "ADMIN_BOOTSTRAPPED",
                      "Initial administrator account provisioned successfully");

        AuthResult result{
            true, userId, name, "ADMIN", "Initial administrator provisioned"
        };
        result.sessionId = sessionId;
        return result;
    } catch (const std::exception&) {
        writeAuditLog("", "ADMIN_BOOTSTRAP_FAILED",
                      "Initial admin provisioning failed due to a system error");
        return {false, "", "", "", "Initial admin provisioning could not be completed"};
    }
}

AuthResult AuthService::login(
    const std::string& email,
    const std::string& password
) {
    try {
        if (!validEmail(email) || password.empty()) {
            writeAuditLog("UNKNOWN", "LOGIN_FAILED",
                          "Login rejected due to invalid input");
            return {false, "", "", "", "Invalid email or password"};
        }

        auto connection = createConnection();
        pqxx::work transaction(connection);

        pqxx::result result = transaction.exec(
            "SELECT user_id, name, email, password_hash, role "
            "FROM users WHERE email = $1",
            pqxx::params{email}
        );

        if (result.empty()) {
            writeAuditLog("UNKNOWN", "LOGIN_FAILED",
                          "Login rejected because the account was not found");
            return {false, "", "", "", "Invalid email or password"};
        }

        const auto& row = result[0];

        std::string storedHash =
            row["password_hash"].as<std::string>();

        if (!PasswordHasher::verifyPassword(password, storedHash)) {
            writeAuditLog(
                row["user_id"].as<std::string>(),
                "LOGIN_FAILED",
                "Login rejected due to incorrect password"
            );
            return {false, "", "", "", "Invalid email or password"};
        }

        std::string userId =
            row["user_id"].as<std::string>();

        std::string name =
            row["name"].as<std::string>();

        std::string role =
            row["role"].as<std::string>();

        transaction.commit();

        std::string sessionId =
            sessionManager.createSession(userId, role);

        AuthResult authResult{
            true,
            userId,
            name,
            role,
            "Login successful"
        };

        authResult.sessionId = sessionId;

        writeAuditLog(userId, "LOGIN_SUCCESS",
                      "User logged in successfully");

        return authResult;

    } catch (const std::exception& e) {
        writeAuditLog("UNKNOWN", "LOGIN_ERROR",
                      "Login could not be completed due to a system error");
        return {false, "", "", "", e.what()};
    }
}

bool AuthService::logout(const std::string& sessionId) {
    std::string userId = sessionManager.getUserId(sessionId);

    bool loggedOut = sessionManager.logout(sessionId);

    if (loggedOut) {
        writeAuditLog(userId, "LOGOUT_SUCCESS",
                      "User logged out successfully");
    } else {
        writeAuditLog("UNKNOWN", "LOGOUT_FAILED",
                      "Logout attempted with an invalid or expired session");
    }

    return loggedOut;
}

bool AuthService::isSessionValid(const std::string& sessionId) {
    return sessionManager.isSessionValid(sessionId);
}

std::string AuthService::getSessionUserId(
    const std::string& sessionId
) {
    return sessionManager.getUserId(sessionId);
}

std::string AuthService::getSessionUserRole(
    const std::string& sessionId
) {
    return sessionManager.getUserRole(sessionId);
}

}
