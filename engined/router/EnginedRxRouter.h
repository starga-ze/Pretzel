#pragma once

#include "router/RxRouter.h"
#include "event/EnginedEvent.h"
#include "event/EnginedEventFactory.h"
#include "service/EnginedServiceManager.h"

namespace nf::engined
{

class EnginedRxRouter : public nf::router::RxRouter
{
public:
    EnginedRxRouter(EnginedEventFactory* eventFactory,
                    EnginedServiceManager* serviceManager);
    ~EnginedRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedServiceManager* m_serviceManager{nullptr};
};

} // namespace nf::engined
