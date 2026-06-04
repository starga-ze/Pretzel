#include "service/probe/ProbeEvent.h"
#include "service/IcmpdServiceManager.h"

namespace nf::icmpd
{

    ProbeEvent::ProbeEvent(ProbeEventType type) :
        IcmpdEvent(IcmpdEventDomain::Probe),
        m_type(type)
    {

    }

    ProbeEvent::ProbeEvent(ProbeEventType type, std::unique_ptr<nf::ipc::IpcMessage> message) :
        IcmpdEvent(IcmpdEventDomain::Probe, std::move(message)),
        m_type(type)
    {

    }

    ProbeEventType ProbeEvent::type() const
    {
        return m_type;
    }

    void ProbeEvent::dispatch(IcmpdServiceManager& serviceManager)
    {
        serviceManager.probeService().handleEvent(serviceManager, *this);
    }

}
