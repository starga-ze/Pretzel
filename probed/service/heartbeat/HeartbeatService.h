#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

namespace pz::probed
{

class ProbedServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(ProbedServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(ProbedServiceManager& serviceManager, const HeartbeatAction& action);
};

}
