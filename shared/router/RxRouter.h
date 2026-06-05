#pragma once

#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::router
{

class RxRouter
{
    public:
        RxRouter() = default;
        virtual ~RxRouter() = default;

        virtual void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage>) = 0;
};

}
