#pragma once

#include "service/heartbeat/HeartbeatEvent.h"
#include "service/heartbeat/HeartbeatAction.h"

namespace pz::authd
{

class AuthdServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(AuthdServiceManager& serviceManager,
                     const HeartbeatEvent& event);

    void handleAction(AuthdServiceManager& serviceManager,
                      const HeartbeatAction& action);
};

} // namespace pz::authd
