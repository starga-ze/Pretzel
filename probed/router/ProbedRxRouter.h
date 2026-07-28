#pragma once

#include "event/ProbedEvent.h"
#include "event/ProbedEventFactory.h"
#include "router/RxRouter.h"
#include "service/ProbedServiceManager.h"

namespace pz::probed
{

class ProbedRxRouter : public pz::router::RxRouter
{
public:
    ProbedRxRouter(ProbedEventFactory* eventFactory);
    ~ProbedRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleIcmpPacket(const std::string& srcIp, std::unique_ptr<IcmpPacket> packet);

    void setServiceManager(ProbedServiceManager* serviceManager);

private:
    ProbedServiceManager* m_serviceManager = nullptr;
    ProbedEventFactory* m_eventFactory = nullptr;
};

}
