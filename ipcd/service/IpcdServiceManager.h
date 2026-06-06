#pragma once

#include "service/ServiceManager.h"

#include "event/IpcdEvent.h"
#include "action/IpcdAction.h"

#include "service/bootstrap/BootstrapService.h"

#include "router/IpcdTxRouter.h"

#include <memory>
#include <queue>

namespace nf::ipcd
{

class IpcdEventFactory;
class IpcdActionFactory;

class IpcdServiceManager : public nf::service::ServiceManager<IpcdEvent, IpcdAction>
{
public:
    IpcdServiceManager(IpcdEventFactory* eventFactory,
                       IpcdActionFactory* actionFactory,
                       IpcServerHandler* ipcServerHandler,
                       IpcdTxRouter* txRouter);
    ~IpcdServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<IpcdEvent> event) override;
    void postAction(std::unique_ptr<IpcdAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();

    IpcdTxRouter& txRouter();

private:
    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdActionFactory* m_actionFactory{nullptr};
    IpcdTxRouter* m_txRouter{nullptr};

    std::unique_ptr<BootstrapService> m_bootstrapService;

    std::queue<std::unique_ptr<IpcdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<IpcdAction>> m_actionQueue;
};

} // namespace nf::ipcd
