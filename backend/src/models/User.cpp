
#include "models/User.h"
#include <stdexcept>

namespace vrms {

User::User(const std::string& id, const std::string& name,
           const std::string& email)
    : userId(id), name(name), email(email) {

    if (id.empty() || name.empty() || email.empty())
        throw std::invalid_argument("User details cannot be empty");
}

std::string User::getUserId() const {
    return userId;
}

std::string User::getName() const {
    return name;
}

std::string User::getEmail() const {
    return email;
}

void User::updateName(const std::string& newName) {
    if (newName.empty())
        throw std::invalid_argument("Name cannot be empty");

    name = newName;
}

void User::updateEmail(const std::string& newEmail) {
    if (newEmail.empty())
        throw std::invalid_argument("Email cannot be empty");

    email = newEmail;
}

}
