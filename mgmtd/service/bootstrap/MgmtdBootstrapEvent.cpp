#include "service/bootstrap/MgmtdBootstrapEvent.h"
#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

MgmtdBootstrapEvent::MgmtdBootstrapEvent(MgmtdBootstrapEventType type)
    : MgmtdEvent(MgmtdEventDomain::Bootstrap),
      m_type(type)
{
}

MgmtdBootstrapEvent::MgmtdBootstrapEvent(MgmtdBootstrapEventType type,
                                         std::unique_ptr<nf::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Bootstrap),
      m_type(type),
      m_message(std::move(message))
{
}

void MgmtdBootstrapEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleEvent(serviceManager, *this);
}

MgmtdBootstrapEventType MgmtdBootstrapEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* MgmtdBootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> MgmtdBootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::mgmtd
