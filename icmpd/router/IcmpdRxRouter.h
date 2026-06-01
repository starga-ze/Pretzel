#pragma once

#include "router/RxRouter.h"
#include "process/IcmpdProcess.h"
#include "event/IcmpdEvent.h"
#include "event/IcmpdEventFactory.h"

namespace nf::icmpd
{

class IcmpdRxRouter : public nf::router::RxRouter
{
public:
    IcmpdRxRouter();
    ~IcmpdRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    void handleEvent(std::unique_ptr<nf::event::Event> event) override;

    void setServiceManager(IcmpdServiceManager* serviceManager);

private:
    IcmpdServiceManager* m_serviceManager = nullptr;

    IcmpdEventFactory m_eventFactory;
};

} // namespace nf::icmpd
