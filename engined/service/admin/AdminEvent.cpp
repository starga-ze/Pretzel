#include "service/admin/AdminEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

AdminEvent::AdminEvent(AdminEventType type)
    : EnginedEvent(EnginedEventDomain::Admin),
      m_type(type)
{
}

AdminEvent::AdminEvent(AdminEventType type,
                       std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Admin),
      m_type(type),
      m_message(std::move(message))
{
}

void AdminEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.adminService().handleEvent(serviceManager, *this);
}

AdminEventType AdminEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* AdminEvent::message() const
{
    return m_message.get();
}

} // namespace pz::engined
