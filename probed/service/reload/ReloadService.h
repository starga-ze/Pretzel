#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::probed
{

class ProbedServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(ProbedServiceManager& serviceManager, const ReloadEvent& event);
};

}
