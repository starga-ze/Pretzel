#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::collectord
{

class CollectordServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(CollectordServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(CollectordServiceManager& serviceManager, const HeartbeatAction& action);
};

}
