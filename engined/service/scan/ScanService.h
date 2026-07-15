#pragma once

#include "service/scan/ScanEvent.h"

#include <chrono>
#include <memory>
#include <string>

namespace pz::engined
{

class EnginedServiceManager;

class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void start();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(EnginedServiceManager& serviceManager, const ScanEvent& event);

private:
    void sendScanRequest(EnginedServiceManager& serviceManager);
    void persistDevices(EnginedServiceManager& serviceManager, const std::string& payloadJson);

    bool m_pending{false};
    std::chrono::steady_clock::time_point m_lastPollAt{};
    std::chrono::steady_clock::time_point m_requestedAt{};
};

}
