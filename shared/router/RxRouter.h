#pragma once

#include "ipc/IpcMessage.h"
#include "event/Event.h"

#include <memory>

namespace nf::router
{

class RxRouter
{
    public:
        RxRouter() = default;
        virtual ~RxRouter() = default;

        virtual void handleMessage(std::unique_ptr<nf::ipc::IpcMessage>) = 0;
        virtual void handleEvent(std::unique_ptr<nf::event::Event>) = 0;
};

}
