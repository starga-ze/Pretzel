#pragma once

#include "service/heartbeat/HeartbeatEvent.h"
#include "service/heartbeat/HeartbeatAction.h"

namespace nf::snmpd
{

class SnmpdServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(SnmpdServiceManager& serviceManager,
                     const HeartbeatEvent& event);

    void handleAction(SnmpdServiceManager& serviceManager,
                      const HeartbeatAction& action);
};

} // namespace nf::snmpd
