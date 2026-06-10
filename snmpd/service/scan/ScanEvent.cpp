#include "service/scan/ScanEvent.h"
#include "service/SnmpdServiceManager.h"

namespace pz::snmpd
{

ScanEvent::ScanEvent(ScanEventType type)
    : SnmpdEvent(SnmpdEventDomain::Scan),
      m_type(type)
{
}

ScanEvent::ScanEvent(ScanEventType type,
                     std::unique_ptr<pz::ipc::IpcMessage> message)
    : SnmpdEvent(SnmpdEventDomain::Scan),
      m_type(type),
      m_message(std::move(message))
{
}

ScanEvent::ScanEvent(ScanEventType type,
                     std::vector<SnmpDevice> devices)
    : SnmpdEvent(SnmpdEventDomain::Scan),
      m_type(type),
      m_devices(std::move(devices))
{
}

void ScanEvent::dispatch(SnmpdServiceManager& serviceManager)
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

const std::vector<SnmpDevice>& ScanEvent::devices() const
{
    return m_devices;
}

} // namespace pz::snmpd
