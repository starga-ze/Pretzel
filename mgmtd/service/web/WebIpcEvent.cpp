#include "service/web/WebIpcEvent.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebService.h"

namespace pz::mgmtd
{

WebIpcEvent::WebIpcEvent(WebIpcEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Web), m_type(type), m_message(std::move(message))
{
}

void WebIpcEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.webService().handleIpcEvent(serviceManager, *this);
}

WebIpcEventType WebIpcEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* WebIpcEvent::message() const
{
    return m_message.get();
}

}
