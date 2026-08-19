#pragma once

#include "grpc/GrpcClientHandler.h"
#include "http/HttpHandler.h"
#include "http/HttpMessage.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"
#include "router/TxRouter.h"
#include "grpc/GrpcMessage.h"

#include <cstdint>
#include <string>

namespace pz::mgmtd
{

class MgmtdTxRouter : public pz::router::TxRouter
{
public:
    MgmtdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, pz::http::HttpHandler* httpHandler,
                  GrpcClientHandler* grpcClientHandler);
    ~MgmtdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleHttpMessage(pz::http::HttpResponse response, pz::http::SessionId id) override;

    // The gRPC egress, alongside IPC and HTTP: one call on its way to the pretzel-ai service.
    // Fire-and-forget like handleIpcMessage — it returns immediately and the answer arrives later
    // as a WebGrpcEvent (GrpcClientHandler::drain -> MgmtdRxRouter, on the main loop). Not an
    // override: gRPC is mgmtd's transport, not part of the shared TxRouter base.
    //
    // One method, not one per call: the command travels inside the message. A router with a method
    // per operation has to be edited every time the service gains one, and that is the point at
    // which it starts holding opinions about what those operations mean. Nothing is read back
    // through here either — state belongs to the service manager, not to a transport router.
    void handleGrpcMessage(GrpcMessage message);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    pz::http::HttpHandler* m_httpHandler = nullptr;
    GrpcClientHandler* m_grpcClientHandler = nullptr;
};

}
