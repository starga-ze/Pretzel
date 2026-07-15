#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::scand
{

class ScandServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(ScandServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(ScandServiceManager& serviceManager, const HeartbeatAction& action);
};

}
