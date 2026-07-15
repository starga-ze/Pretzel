#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::scand
{

class ScandServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(ScandServiceManager& serviceManager, const ReloadEvent& event);
};

}
