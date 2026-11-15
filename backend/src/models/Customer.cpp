
#include "models/Customer.h"
#include <stdexcept>

namespace vrms {

Customer::Customer(
    const std::string& id,
    const std::string& name,
    const std::string& email,
    const std::string& membership
)
    : User(id, name, email), membershipLevel(membership) {

    if (membership.empty()) {
        throw std::invalid_argument("Membership cannot be empty");
    }
}

std::string Customer::getMembershipLevel() const {
    return membershipLevel;
}

void Customer::setMembershipLevel(const std::string& level) {
    if (level.empty()) {
        throw std::invalid_argument("Invalid membership level");
    }

    membershipLevel = level;
}

std::string Customer::getRole() const {
    return "CUSTOMER";
}

}
