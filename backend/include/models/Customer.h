
#ifndef CUSTOMER_H
#define CUSTOMER_H

#include "models/User.h"

namespace vrms {

class Customer : public User {
private:
    std::string membershipLevel;

public:
    Customer(const std::string& id, const std::string& name,
             const std::string& email,
             const std::string& membership = "Regular");

    std::string getMembershipLevel() const;
    void setMembershipLevel(const std::string& level);

    std::string getRole() const override;
};

}

#endif
