#pragma once

#include "event/ApidEvent.h"
#include "event/ApidEventFactory.h"
#include "router/RxRouter.h"
#include "service/ApidServiceManager.h"

#include "http/HttpMessage.h"

#include <memory>

namespace pz::apid
{

class ApidRxRouter : public pz::router::RxRouter
{
public:
    ApidRxRouter(ApidEventFactory* eventFactory, ApidServiceManager* serviceManager);
    ~ApidRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleHttpMessage(pz::http::HttpRequest req, pz::http::SessionId id) override;

private:
    ApidEventFactory* m_eventFactory{nullptr};
    ApidServiceManager* m_serviceManager{nullptr};
};

}
