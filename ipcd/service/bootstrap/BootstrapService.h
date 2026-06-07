#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <memory>

namespace pz::ipcd
{

class IpcdServiceManager;
class IpcdEventFactory;
class IpcdActionFactory;
class IpcServerHandler;

class BootstrapService
{
public:
    BootstrapService(IpcdEventFactory* eventFactory,
                         IpcdActionFactory* actionFactory,
                         IpcServerHandler* ipcServerHandler);

    ~BootstrapService() = default;

    void handleEvent(IpcdServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(IpcdServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    std::unique_ptr<pz::ipc::IpcMessage> buildServerHello(const pz::ipc::IpcMessage& req) const;
    std::unique_ptr<pz::ipc::IpcMessage> buildSyncResponse(const pz::ipc::IpcMessage& req) const;

    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdActionFactory* m_actionFactory{nullptr};
    IpcServerHandler* m_ipcServerHandler{nullptr};
};

} // namespace pz::ipcd
