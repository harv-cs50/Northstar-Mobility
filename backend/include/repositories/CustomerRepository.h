
#ifndef CUSTOMER_REPOSITORY_H
#define CUSTOMER_REPOSITORY_H

#include "models/Customer.h"
#include <pqxx/pqxx>
#include <vector>
#include <string>

namespace vrms {

struct CustomerRecord {
    std::string id;
    std::string name;
    std::string email;
    std::string membership;
};

class CustomerRepository {
private:
    pqxx::connection connection;

public:
    CustomerRepository();

    void saveCustomer(
        const Customer& customer,
        const std::string& passwordHash
    );

    std::vector<CustomerRecord> getAllCustomers();

    CustomerRecord getCustomerById(const std::string& id);
};

}

#endif
