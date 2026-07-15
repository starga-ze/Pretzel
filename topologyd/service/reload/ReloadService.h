#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::topologyd
{

class TopologydServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(TopologydServiceManager& serviceManager, const ReloadEvent& event);
};

}
