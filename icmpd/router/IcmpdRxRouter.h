#pragma once

#include "router/RxRouter.h"
#include "event/IcmpdEvent.h"
#include "event/IcmpdEventFactory.h"
#include "service/IcmpdServiceManager.h"

namespace pz::icmpd
{

class IcmpdRxRouter : public pz::router::RxRouter
{
public:
    IcmpdRxRouter(IcmpdEventFactory* eventFactory);
    ~IcmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleIcmpPacket(const std::string& srcIp, std::unique_ptr<IcmpPacket> packet);

    void setServiceManager(IcmpdServiceManager* serviceManager);

private:
    IcmpdServiceManager* m_serviceManager = nullptr;
    IcmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::icmpd
