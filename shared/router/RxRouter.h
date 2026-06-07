#pragma once

#include "ipc/IpcMessage.h"

#include <memory>

namespace pz::router
{

class RxRouter
{
    public:
        RxRouter() = default;
        virtual ~RxRouter() = default;

        virtual void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage>) = 0;
};

}
