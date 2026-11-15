
#include "repositories/CustomerRepository.h"
#include "DatabaseConfig.h"
#include <stdexcept>

namespace vrms {

CustomerRepository::CustomerRepository()
    : connection(createDatabaseConnection()) {}

void CustomerRepository::saveCustomer(
    const Customer& customer,
    const std::string& passwordHash
) {
    if (passwordHash.empty()) {
        throw std::invalid_argument(
            "Password hash cannot be empty"
        );
    }

    pqxx::work transaction(connection);

    transaction.exec(
        "INSERT INTO users "
        "(user_id, name, email, password_hash, role, membership_level) "
        "VALUES ($1, $2, $3, $4, 'CUSTOMER', $5) "
        "ON CONFLICT (user_id) DO UPDATE SET "
        "name = EXCLUDED.name, "
        "email = EXCLUDED.email, "
        "membership_level = EXCLUDED.membership_level",
        pqxx::params{
            customer.getUserId(),
            customer.getName(),
            customer.getEmail(),
            passwordHash,
            customer.getMembershipLevel()
        }
    );

    transaction.commit();
}

std::vector<CustomerRecord>
CustomerRepository::getAllCustomers() {

    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT user_id, name, email, membership_level "
        "FROM users "
        "WHERE role = 'CUSTOMER' "
        "ORDER BY user_id"
    );

    std::vector<CustomerRecord> customers;

    for (const auto& row : result) {
        customers.push_back({
            row["user_id"].as<std::string>(),
            row["name"].as<std::string>(),
            row["email"].as<std::string>(),
            row["membership_level"].as<std::string>()
        });
    }

    transaction.commit();

    return customers;
}

CustomerRecord CustomerRepository::getCustomerById(
    const std::string& id
) {
    pqxx::work transaction(connection);

    pqxx::result result = transaction.exec(
        "SELECT user_id, name, email, membership_level "
        "FROM users "
        "WHERE user_id = $1 AND role = 'CUSTOMER'",
        pqxx::params{id}
    );

    if (result.empty()) {
        throw std::runtime_error("Customer not found");
    }

    CustomerRecord customer{
        result[0]["user_id"].as<std::string>(),
        result[0]["name"].as<std::string>(),
        result[0]["email"].as<std::string>(),
        result[0]["membership_level"].as<std::string>()
    };

    transaction.commit();

    return customer;
}

}
