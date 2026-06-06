#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <memory>

namespace nf::ipcd
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
    std::unique_ptr<nf::ipc::IpcMessage> buildServerHello(const nf::ipc::IpcMessage& req) const;
    std::unique_ptr<nf::ipc::IpcMessage> buildSyncResponse(const nf::ipc::IpcMessage& req) const;

    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdActionFactory* m_actionFactory{nullptr};
    IpcServerHandler* m_ipcServerHandler{nullptr};
};

} // namespace nf::ipcd
