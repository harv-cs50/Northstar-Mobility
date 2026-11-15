
#ifndef AUTHORIZATION_SERVICE_H
#define AUTHORIZATION_SERVICE_H

#include "models/User.h"
#include "models/Admin.h"

#include <string>

namespace vrms {

class AuthorizationService {
public:
    bool isCustomer(const User& user) const;

    bool isAdmin(const User& user) const;

    bool canAccessAdminPanel(const User& user) const;

    // Admin provisioning uses the role stored in PostgreSQL. The current
    // schema does not persist access levels, so no client-supplied level is used.
    bool canProvisionAdmins(const User& user) const;

    bool hasAdminPermission(
        const Admin& admin,
        int requiredLevel
    ) const;

    bool canManageVehicles(const Admin& admin) const;

    bool canManageUsers(const Admin& admin) const;

    bool canManagePayments(const Admin& admin) const;

    bool canViewReports(const Admin& admin) const;
};

}

#endif
