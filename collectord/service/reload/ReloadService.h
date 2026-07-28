#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::collectord
{

class CollectordServiceManager;

class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(CollectordServiceManager& serviceManager, const ReloadEvent& event);
};

}
