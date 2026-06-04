#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"

#include <queue>

namespace nf::icmpd
{

class IcmpdServiceManager : public nf::service::ServiceManager<IcmpdEvent>
{
public:
    IcmpdServiceManager(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory);
    ~IcmpdServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<IcmpdEvent> event) override;
    void execute() override;

    BootstrapService& bootstrapService();

private:
    IcmpdEventFactory* m_eventFactory;
    IcmpdActionFactory* m_actionFactory;   

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::queue<std::unique_ptr<IcmpdEvent>> m_eventQueue;
};

} // namespace nf::icmpd
