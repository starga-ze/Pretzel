#include "service/web/WebEvent.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebService.h"

#include <utility>

namespace pz::mgmtd
{

WebEvent::WebEvent(pz::http::HttpRequest request, pz::http::SessionId sessionId)
    : MgmtdEvent(MgmtdEventDomain::Web), m_type(WebEventType::Http), m_request(std::move(request)),
      m_sessionId(sessionId)
{
}

WebEvent::WebEvent(WebIpcEventType ipcType, std::unique_ptr<pz::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Web), m_type(WebEventType::Ipc), m_ipcType(ipcType),
      m_message(std::move(message))
{
}

WebEvent::WebEvent(WebGrpcEventType grpcType, std::uint32_t ticket, std::string json)
    : MgmtdEvent(MgmtdEventDomain::Web), m_type(WebEventType::Grpc), m_grpcType(grpcType), m_ticket(ticket),
      m_json(std::move(json))
{
}

void WebEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.webService().handleEvent(serviceManager, *this);
}

WebEventType WebEvent::type() const
{
    return m_type;
}

const pz::http::HttpRequest& WebEvent::request() const
{
    return m_request;
}

pz::http::SessionId WebEvent::sessionId() const
{
    return m_sessionId;
}

WebIpcEventType WebEvent::ipcType() const
{
    return m_ipcType;
}

const pz::ipc::IpcMessage* WebEvent::message() const
{
    return m_message.get();
}

WebGrpcEventType WebEvent::grpcType() const
{
    return m_grpcType;
}

std::uint32_t WebEvent::ticket() const
{
    return m_ticket;
}

const std::string& WebEvent::json() const
{
    return m_json;
}

std::string WebEvent::takeJson()
{
    return std::move(m_json);
}

}
