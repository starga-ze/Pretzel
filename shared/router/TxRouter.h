#pragma once

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
};

}
