#pragma once

#include "service/reload/ReloadEvent.h"

namespace pz::icmpd
{

class IcmpdServiceManager;

// Handles the ConfigReload IPC command by scheduling a daemon restart. This lives in
// a service (not inline in the RxRouter) so the router stays a pure pass-through —
// even a trivial reaction belongs under service/.
class ReloadService
{
public:
    ReloadService() = default;
    ~ReloadService() = default;

    void handleEvent(IcmpdServiceManager& serviceManager, const ReloadEvent& event);
};

} // namespace pz::icmpd
