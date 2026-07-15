#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::authd
{

class AuthdServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(AuthdServiceManager& serviceManager, const ReloadEvent& event);
};

}
