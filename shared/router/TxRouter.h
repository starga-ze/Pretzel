#pragma once

#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::router
{

class TxRouter
{
    public:
        TxRouter() = default;
        virtual ~TxRouter() = default;

        virtual void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage>) = 0;
};

}
