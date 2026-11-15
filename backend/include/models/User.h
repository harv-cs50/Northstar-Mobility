
#ifndef USER_H
#define USER_H

#include <string>

namespace vrms {

class User {
private:
    std::string userId;
    std::string name;
    std::string email;

public:
    User(const std::string& id, const std::string& name,
         const std::string& email);

    virtual ~User() = default;

    std::string getUserId() const;
    std::string getName() const;
    std::string getEmail() const;

    void updateName(const std::string& newName);
    void updateEmail(const std::string& newEmail);

    virtual std::string getRole() const = 0;
};

}

#endif
