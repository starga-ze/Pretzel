#pragma once

#include "service/admin/AdminEvent.h"

namespace pz::engined
{

class EnginedServiceManager;

class AdminService
{
public:
    AdminService() = default;
    ~AdminService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const AdminEvent& event);

private:
    void updatePassword(const std::string& payloadJson);
};

}
