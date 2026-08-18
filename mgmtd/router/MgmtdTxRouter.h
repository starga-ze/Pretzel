#pragma once

#include "grpc/GrpcClientHandler.h"
#include "http/HttpHandler.h"
#include "http/HttpMessage.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"
#include "router/TxRouter.h"

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

    // The gRPC egress, alongside IPC and HTTP: a chat turn on its way to the pretzel-ai inference
    // service. Fire-and-forget like handleIpcMessage — it returns immediately and the answer is
    // filed later under `ticket` (GrpcClientHandler::drain, on the main loop). Not an override:
    // gRPC is mgmtd's transport, not part of the shared TxRouter base.
    void handleGrpcMessage(std::uint32_t ticket, std::string model, std::string message,
                           std::string systemPrompt);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    pz::http::HttpHandler* m_httpHandler = nullptr;
    GrpcClientHandler* m_grpcClientHandler = nullptr;
};

}
