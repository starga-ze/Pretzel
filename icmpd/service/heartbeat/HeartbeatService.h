#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::icmpd
{

class IcmpdServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(IcmpdServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(IcmpdServiceManager& serviceManager, const HeartbeatAction& action);
};

}
