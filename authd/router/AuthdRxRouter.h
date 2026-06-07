#pragma once

#include "router/RxRouter.h"
#include "event/AuthdEvent.h"
#include "event/AuthdEventFactory.h"
#include "service/AuthdServiceManager.h"

namespace pz::authd
{

class AuthdRxRouter : public pz::router::RxRouter
{
public:
    AuthdRxRouter(AuthdEventFactory* eventFactory);
    ~AuthdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setServiceManager(AuthdServiceManager* serviceManager);

private:
    AuthdServiceManager* m_serviceManager = nullptr;
    AuthdEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::authd
