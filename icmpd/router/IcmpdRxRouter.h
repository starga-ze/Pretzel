#pragma once

#include "router/RxRouter.h"
#include "event/IcmpdEvent.h"
#include "event/IcmpdEventFactory.h"
#include "service/IcmpdServiceManager.h"

namespace nf::icmpd
{

class IcmpdRxRouter : public nf::router::RxRouter
{
public:
    IcmpdRxRouter(IcmpdEventFactory* eventFactory);
    ~IcmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setServiceManager(IcmpdServiceManager* serviceManager);

private:
    IcmpdServiceManager* m_serviceManager = nullptr;
    IcmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace nf::icmpd
