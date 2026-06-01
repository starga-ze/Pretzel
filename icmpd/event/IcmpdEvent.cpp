#include "event/IcmpdEvent.h"

namespace nf::icmpd
{

IcmpdEvent::IcmpdEvent(IcmpdEventType type)
    : m_type(type)
{
}

IcmpdEvent::IcmpdEvent(IcmpdEventType type, std::unique_ptr<nf::ipc::IpcMessage> message)
    : m_type(type), m_message(std::move(message))
{
}

IcmpdEventType IcmpdEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* IcmpdEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> IcmpdEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::icmpd
