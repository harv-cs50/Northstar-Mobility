
#include "models/Admin.h"
#include <stdexcept>

namespace vrms {

Admin::Admin(
    const std::string& id,
    const std::string& name,
    const std::string& email,
    int accessLevel
)
    : User(id, name, email), accessLevel(accessLevel) {

    if (accessLevel < 1 || accessLevel > 5) {
        throw std::invalid_argument(
            "Access level must be between 1 and 5"
        );
    }
}

int Admin::getAccessLevel() const {
    return accessLevel;
}

void Admin::setAccessLevel(int level) {
    if (level < 1 || level > 5) {
        throw std::invalid_argument("Invalid access level");
    }

    accessLevel = level;
}

std::string Admin::getRole() const {
    return "ADMIN";
}

}
