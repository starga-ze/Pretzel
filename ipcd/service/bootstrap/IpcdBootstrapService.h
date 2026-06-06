#pragma once

#include "service/bootstrap/IpcdBootstrapEvent.h"
#include "service/bootstrap/IpcdBootstrapAction.h"

#include <memory>

namespace nf::ipcd
{

class IpcdServiceManager;
class IpcdEventFactory;
class IpcdActionFactory;
class IpcServerHandler;

class IpcdBootstrapService
{
public:
    IpcdBootstrapService(IpcdEventFactory* eventFactory,
                         IpcdActionFactory* actionFactory,
                         IpcServerHandler* ipcServerHandler);

    ~IpcdBootstrapService() = default;

    void handleEvent(IpcdServiceManager& serviceManager,
                     const IpcdBootstrapEvent& event);

    void handleAction(IpcdServiceManager& serviceManager,
                      const IpcdBootstrapAction& action);

private:
    std::unique_ptr<nf::ipc::IpcMessage> buildServerHello(const nf::ipc::IpcMessage& req) const;
    std::unique_ptr<nf::ipc::IpcMessage> buildSyncResponse(const nf::ipc::IpcMessage& req) const;

    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdActionFactory* m_actionFactory{nullptr};
    IpcServerHandler* m_ipcServerHandler{nullptr};
};

} // namespace nf::ipcd
