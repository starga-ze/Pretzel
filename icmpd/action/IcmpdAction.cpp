#include "action/IcmpdAction.h"

namespace nf::icmpd
{

IcmpdAction::IcmpdAction(IcmpdActionDomain domain)
    : m_domain(domain)
{
}

IcmpdAction::IcmpdAction(
    IcmpdActionDomain domain,
    std::unique_ptr<nf::ipc::IpcMessage> message)
    : m_domain(domain),
      m_message(std::move(message))
{
}

IcmpdActionDomain IcmpdAction::domain() const
{
    return m_domain;
}

const nf::ipc::IpcMessage* IcmpdAction::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> IcmpdAction::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::icmpd
