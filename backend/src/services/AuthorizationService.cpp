
#include "services/AuthorizationService.h"

namespace vrms {

bool AuthorizationService::isCustomer(const User& user) const {
    return user.getRole() == "CUSTOMER";
}

bool AuthorizationService::isAdmin(const User& user) const {
    return user.getRole() == "ADMIN";
}

bool AuthorizationService::canAccessAdminPanel(
    const User& user
) const {
    return isAdmin(user);
}

bool AuthorizationService::canProvisionAdmins(const User& user) const {
    return isAdmin(user);
}

bool AuthorizationService::hasAdminPermission(
    const Admin& admin,
    int requiredLevel
) const {
    if (requiredLevel < 1 || requiredLevel > 5) {
        return false;
    }

    return admin.getAccessLevel() >= requiredLevel;
}

bool AuthorizationService::canManageVehicles(
    const Admin& admin
) const {
    return hasAdminPermission(admin, 2);
}

bool AuthorizationService::canManageUsers(
    const Admin& admin
) const {
    return hasAdminPermission(admin, 4);
}

bool AuthorizationService::canManagePayments(
    const Admin& admin
) const {
    return hasAdminPermission(admin, 4);
}

bool AuthorizationService::canViewReports(
    const Admin& admin
) const {
    return hasAdminPermission(admin, 5);
}

}
