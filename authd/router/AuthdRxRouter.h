#pragma once

#include "router/RxRouter.h"
#include "event/AuthdEvent.h"
#include "event/AuthdEventFactory.h"
#include "service/AuthdServiceManager.h"

namespace nf::authd
{

class AuthdRxRouter : public nf::router::RxRouter
{
public:
    AuthdRxRouter(AuthdEventFactory* eventFactory);
    ~AuthdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setServiceManager(AuthdServiceManager* serviceManager);

private:
    AuthdServiceManager* m_serviceManager = nullptr;
    AuthdEventFactory* m_eventFactory = nullptr;
};

} // namespace nf::authd
