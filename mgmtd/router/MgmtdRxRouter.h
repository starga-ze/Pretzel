#pragma once

#include "event/MgmtdEvent.h"
#include "event/MgmtdEventFactory.h"
#include "router/RxRouter.h"
#include "service/MgmtdServiceManager.h"

#include "http/HttpMessage.h"

#include <memory>

namespace pz::mgmtd
{

class MgmtdRxRouter : public pz::router::RxRouter
{
public:
    MgmtdRxRouter(MgmtdEventFactory* eventFactory, MgmtdServiceManager* serviceManager);
    ~MgmtdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleHttpMessage(pz::http::HttpRequest req, pz::http::SessionId id) override;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

}
