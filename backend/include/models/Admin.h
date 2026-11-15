
#ifndef ADMIN_H
#define ADMIN_H

#include "models/User.h"

namespace vrms {

class Admin : public User {
private:
    int accessLevel;

public:
    Admin(const std::string& id, const std::string& name,
          const std::string& email, int accessLevel);

    int getAccessLevel() const;
    void setAccessLevel(int level);

    std::string getRole() const override;
};

}

#endif
