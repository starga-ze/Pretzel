#include "event/IcmpdEvent.h"

namespace nf::icmpd
{

IcmpdEvent::IcmpdEvent(IcmpdEventDomain domain) : m_domain(domain)
{
}

IcmpdEvent::IcmpdEvent(IcmpdEventDomain domain, std::unique_ptr<nf::ipc::IpcMessage> message) : 
    m_domain(domain), 
    m_message(std::move(message))
{
}

IcmpdEventDomain IcmpdEvent::domain() const
{
    return m_domain;
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
