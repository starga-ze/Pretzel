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

        virtual void handleMessage(std::unique_ptr<nf::ipc::IpcMessage>) = 0;
};

}
