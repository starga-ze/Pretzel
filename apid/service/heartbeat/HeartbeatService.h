#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::apid
{

class ApidServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(ApidServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(ApidServiceManager& serviceManager, const HeartbeatAction& action);
};

}
