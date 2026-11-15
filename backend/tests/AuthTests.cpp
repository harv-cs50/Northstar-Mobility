#include "DatabaseConfig.h"
#include "models/Admin.h"
#include "models/Customer.h"
#include "services/AuthService.h"
#include "services/AuthorizationService.h"

#include <array>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "[PASS] " << message << '\n';
}

void runUnitTests() {
    vrms::AuthorizationService authorization;
    vrms::Customer customer("C1", "Customer", "customer@example.test", "Regular");
    vrms::Admin admin("A1", "Admin", "admin@example.test", 1);
    require(!authorization.canProvisionAdmins(customer),
            "Customer role cannot provision administrators");
    require(authorization.canProvisionAdmins(admin),
            "Administrator role may provision administrators");

    const char* priorHostValue = std::getenv("PGHOST");
    const std::string priorHost = priorHostValue ? priorHostValue : "";
    const bool hadHost = priorHostValue != nullptr;
    // The authorization-only test must not write an audit row to any configured
    // application database. With PGHOST empty, DatabaseConfig refuses a DB
    // connection and AuthService's audit helper safely reports no write.
    if (_putenv_s("PGHOST", "") != 0)
        throw std::runtime_error("Could not isolate AuthService unit test from PostgreSQL");
    vrms::AuthService auth;
    const auto unauthenticated = auth.registerAdmin(
        "A2", "Unauthorized", "unauthorized@example.test", "synthetic-test-input", "invalid-session");
    require(!unauthenticated.success &&
            unauthenticated.message == "Administrator authorization required",
            "Admin creation rejects an unauthenticated caller before database access");

    const auto bootstrapDenied = auth.registerInitialAdmin(
        "A3", "Unauthorized", "bootstrap@example.test", "synthetic-test-input", "");
    require(!bootstrapDenied.success,
            "Initial admin setup rejects a missing bootstrap token");
    if (_putenv_s("PGHOST", hadHost ? priorHost.c_str() : "") != 0)
        throw std::runtime_error("Could not restore PGHOST after AuthService unit test");
}

std::string uniqueSuffix() {
    const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(value);
}

void setEnvironment(const char* name, const std::string& value) {
    if (_putenv_s(name, value.c_str()) != 0)
        throw std::runtime_error(std::string("Could not set ") + name);
}

class TestSchema {
public:
    explicit TestSchema(const std::string& suffix)
        : schema("vrms_auth_it_" + suffix) {
        auto connection = vrms::createDatabaseConnection();
        {
            pqxx::read_transaction verify(connection);
            const auto result = verify.exec("SELECT current_database()");
            require(result[0][0].as<std::string>() == "vrms_auth_test",
                    "Connected database is exactly vrms_auth_test before test setup");
        }

        pqxx::work transaction(connection);
        transaction.exec("CREATE SCHEMA " + transaction.quote_name(schema));
        transaction.exec("SET search_path TO " + transaction.quote_name(schema));
        transaction.exec(
            "CREATE TABLE users ("
            "user_id varchar PRIMARY KEY, name varchar NOT NULL, email varchar NOT NULL UNIQUE, "
            "password_hash text NOT NULL, role varchar NOT NULL CHECK (role IN ('CUSTOMER','ADMIN')), "
            "membership_level varchar DEFAULT 'Regular', "
            "created_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP)"
        );
        transaction.exec(
            "CREATE TABLE audit_logs ("
            "log_id varchar PRIMARY KEY, user_id varchar NOT NULL, action varchar NOT NULL, "
            "description text NOT NULL, created_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP)"
        );
        transaction.commit();

        setEnvironment("PGOPTIONS", "-c search_path=" + schema);
        active = true;
    }

    ~TestSchema() {
        if (!active) return;
        try {
            setEnvironment("PGOPTIONS", "");
            auto connection = vrms::createDatabaseConnection();
            pqxx::read_transaction verify(connection);
            const auto result = verify.exec("SELECT current_database()");
            if (result[0][0].as<std::string>() != "vrms_auth_test") return;
            verify.commit();
            pqxx::work cleanup(connection);
            cleanup.exec("DROP SCHEMA " + cleanup.quote_name(schema) + " CASCADE");
            cleanup.commit();
        } catch (const std::exception& error) {
            std::cerr << "[CLEANUP WARNING] Auth test schema retained: "
                      << error.what() << '\n';
        }
    }

    TestSchema(const TestSchema&) = delete;
    TestSchema& operator=(const TestSchema&) = delete;

private:
    std::string schema;
    bool active = false;
};

void runIntegrationTests() {
    const char* allowed = std::getenv("VRMS_ALLOW_AUTH_TEST_DB");
    const char* database = std::getenv("PGDATABASE");
    const char* host = std::getenv("PGHOST");
    require(allowed && std::string(allowed) == "1" && database &&
            std::string(database) == "vrms_auth_test" && host &&
            std::string(host) == "127.0.0.1",
            "Integration mode requires explicit loopback vrms_auth_test configuration");

    const auto suffix = uniqueSuffix();
    TestSchema schema(suffix);
    const auto password = "Integration-" + suffix + "!Pass";
    const auto bootstrapToken = "test-token-" + suffix + "-not-for-production";
    setEnvironment("VRMS_ADMIN_BOOTSTRAP_TOKEN", bootstrapToken);

    vrms::AuthService customerService;
    const auto customer = customerService.registerCustomer(
        "auth-it-customer-" + suffix, "Integration Customer",
        "auth-it-customer-" + suffix + "@example.test", password);
    require(customer.success && customer.role == "CUSTOMER",
            "Normal customer registration assigns the CUSTOMER role");
    const auto customerLogin = customerService.login(
        "auth-it-customer-" + suffix + "@example.test", password);
    require(customerLogin.success && !customerLogin.sessionId.empty(),
            "New customer can sign in and receive a session");

    const auto unauthorized = customerService.registerAdmin(
        "auth-it-unauthorized-" + suffix, "Should Not Exist",
        "auth-it-unauthorized-" + suffix + "@example.test", password,
        customerLogin.sessionId);
    require(!unauthorized.success,
            "An authenticated customer cannot create an administrator");

    std::array<vrms::AuthService, 2> services;
    std::array<vrms::AuthResult, 2> results;
    std::array<std::string, 2> errors;
    std::barrier start(3);
    auto provision = [&](std::size_t index) {
        start.arrive_and_wait();
        try {
            results[index] = services[index].registerInitialAdmin(
                "auth-it-admin-" + std::to_string(index) + "-" + suffix,
                "Integration Admin",
                "auth-it-admin-" + std::to_string(index) + "-" + suffix + "@example.test",
                password, bootstrapToken);
        } catch (const std::exception& e) { errors[index] = e.what(); }
    };
    std::thread first(provision, 0), second(provision, 1);
    start.arrive_and_wait();
    first.join(); second.join();
    const int successes = static_cast<int>(results[0].success) +
                          static_cast<int>(results[1].success);
    require(successes == 1,
            "Concurrent initial-admin requests allow exactly one winner");
    {
        auto connection = vrms::createDatabaseConnection();
        pqxx::read_transaction transaction(connection);
        const auto count = transaction.exec(
            "SELECT COUNT(*) FROM users WHERE role = 'ADMIN'"
        );
        require(count[0][0].as<int>() == 1,
                "Concurrent initial provisioning leaves exactly one admin row");
    }
    const std::size_t winner = results[0].success ? 0 : 1;
    const auto adminLogin = services[winner].login(
        "auth-it-admin-" + std::to_string(winner) + "-" + suffix + "@example.test",
        password);
    require(adminLogin.success && adminLogin.role == "ADMIN",
            "Provisioned administrator can sign in");

    const auto secondAdmin = services[winner].registerAdmin(
        "auth-it-admin-authorized-" + suffix, "Authorized Admin",
        "auth-it-admin-authorized-" + suffix + "@example.test", password,
        adminLogin.sessionId);
    require(secondAdmin.success && secondAdmin.role == "ADMIN",
            "Authenticated administrator can provision another administrator");
}

}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--integration")
            runIntegrationTests();
        else
            runUnitTests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
