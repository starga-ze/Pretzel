#include "service/probe/MgmtdProbeEvent.h"
#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

MgmtdProbeEvent::MgmtdProbeEvent(MgmtdProbeEventType type)
    : MgmtdEvent(MgmtdEventDomain::Probe),
      m_type(type)
{
}

MgmtdProbeEvent::MgmtdProbeEvent(MgmtdProbeEventType type,
                                  std::unique_ptr<nf::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Probe),
      m_type(type),
      m_message(std::move(message))
{
}

void MgmtdProbeEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.probeService().handleEvent(serviceManager, *this);
}

MgmtdProbeEventType MgmtdProbeEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* MgmtdProbeEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> MgmtdProbeEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::mgmtd
