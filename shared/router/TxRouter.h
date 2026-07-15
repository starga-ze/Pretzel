#pragma once

#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace pz::router
{

class TxRouter
{
public:
    TxRouter() = default;
    virtual ~TxRouter() = default;

    virtual void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage>) = 0;

    virtual void handleHttpMessage(pz::http::HttpResponse, pz::http::SessionId){};
};

}
