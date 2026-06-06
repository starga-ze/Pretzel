#pragma once

#include "service/heartbeat/HeartbeatEvent.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace nf::mgmtd
{

class MgmtdServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void handleEvent(MgmtdServiceManager& serviceManager,
                     const HeartbeatEvent& event);

    std::string latestJson() const;
    bool hasData() const;

private:
    mutable std::atomic<bool> m_hasData{false};
    std::string m_latestJson;
};

} // namespace nf::mgmtd
