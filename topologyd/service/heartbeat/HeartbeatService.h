#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::topologyd
{

class TopologydServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(TopologydServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(TopologydServiceManager& serviceManager, const HeartbeatAction& action);
};

}
