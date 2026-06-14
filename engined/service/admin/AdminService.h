#pragma once

#include "service/admin/AdminEvent.h"

namespace pz::engined
{

class EnginedServiceManager;

// Persists login credential updates into the local_users table (a non-versioned store,
// so password changes don't create running_config versions). engined is the single DB
// writer, so mgmtd forwards password changes here (it computes the hash+salt and applies
// them in-memory optimistically).
class AdminService
{
public:
    AdminService() = default;
    ~AdminService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const AdminEvent& event);

private:
    void updatePassword(const std::string& payloadJson);
};

} // namespace pz::engined
