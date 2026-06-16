#pragma once

#include "service/heartbeat/HeartbeatEvent.h"
#include "service/heartbeat/HeartbeatAction.h"

namespace pz::scand
{

class ScandServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(ScandServiceManager& serviceManager,
                     const HeartbeatEvent& event);

    void handleAction(ScandServiceManager& serviceManager,
                      const HeartbeatAction& action);
};

} // namespace pz::scand
