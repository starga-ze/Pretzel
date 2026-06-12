#include "service/scan/ScanEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

ScanEvent::ScanEvent(ScanEventType type)
    : EnginedEvent(EnginedEventDomain::Scan),
      m_type(type)
{
}

ScanEvent::ScanEvent(ScanEventType type,
                     std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Scan),
      m_type(type),
      m_message(std::move(message))
{
}

void ScanEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.scanService().handleEvent(serviceManager, *this);
}

ScanEventType ScanEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* ScanEvent::message() const
{
    return m_message.get();
}

} // namespace pz::engined
