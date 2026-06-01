#include "event/IcmpdEvent.h"
#include "event/IcmpdEventFactory.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::icmpd
{

std::unique_ptr<nf::event::Event> IcmpdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<nf::event::Event> IcmpdEventFactory::create(std::unique_ptr<nf::ipc::IpcMessage> message)
{
    if (!message)
    {
        LOG_WARN("IcmpdEventFactory: null message");
        return nullptr;
    }

    switch (message->getCmd())
    {
    default:
        LOG_WARN("IcmpdEventFactory: unhandled cmd={}", static_cast<int>(message->getCmd()));
        return nullptr;
    }

    return nullptr;
}

} // namespace nf::icmpd
