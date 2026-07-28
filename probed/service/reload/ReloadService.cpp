#include "service/reload/ReloadService.h"

#include "core/Core.h"
#include "util/Logger.h"

namespace pz::probed
{

void ReloadService::handleEvent(ProbedServiceManager&, const ReloadEvent& event)
{
    if (event.type() != ReloadEventType::ReceiveConfigReload)
    {
        return;
    }

    LOG_INFO("config reload received — scheduling daemon restart");
    pz::core::Core::scheduleReload();
}

}
