#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::icmpd
{

class IcmpdServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(IcmpdServiceManager& serviceManager, const ReloadEvent& event);
};

}
