#pragma once

#include "event/MgmtdEvent.h"
#include "event/MgmtdEventFactory.h"
#include "router/RxRouter.h"
#include "grpc/GrpcProtocol.h"
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

    // An answer from pretzel-ai, arriving on the main loop from GrpcClientHandler::drain(). Turned
    // into a WebGrpcEvent and posted, exactly as an inbound IPC message is: the transport reports
    // what came back, and the controller that asked for it decides what it means. Not an override —
    // gRPC is mgmtd's transport, not part of the shared RxRouter base.
    void handleGrpcMessage(GrpcCmd cmd, std::uint32_t ticket, std::string json);

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

}
