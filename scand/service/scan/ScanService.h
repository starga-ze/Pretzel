#pragma once

#include "service/scan/ScanEvent.h"
#include "service/scan/ScanAction.h"

namespace pz::scand
{

class ScandServiceManager;

class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void handleEvent(ScandServiceManager& sm, const ScanEvent& event);
    void handleAction(ScandServiceManager& sm, const ScanAction& action);
};

} // namespace pz::scand
