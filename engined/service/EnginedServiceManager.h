#pragma once

#include "service/ServiceManager.h"

#include "event/EnginedEvent.h"
#include "action/EnginedAction.h"

#include "service/bootstrap/BootstrapService.h"

#include "router/EnginedTxRouter.h"

#include <memory>
#include <queue>

namespace nf::engined
{

class EnginedEventFactory;
class EnginedActionFactory;

class EnginedServiceManager : public nf::service::ServiceManager<EnginedEvent, EnginedAction>
{
public:
    EnginedServiceManager(EnginedEventFactory* eventFactory,
                          EnginedActionFactory* actionFactory,
                          EnginedTxRouter* txRouter);
    ~EnginedServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<EnginedEvent> event) override;
    void postAction(std::unique_ptr<EnginedAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();

    EnginedTxRouter& txRouter();

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedActionFactory* m_actionFactory{nullptr};
    EnginedTxRouter* m_txRouter{nullptr};

    std::unique_ptr<BootstrapService> m_bootstrapService;

    std::queue<std::unique_ptr<EnginedEvent>> m_eventQueue;
    std::queue<std::unique_ptr<EnginedAction>> m_actionQueue;
};

} // namespace nf::engined
