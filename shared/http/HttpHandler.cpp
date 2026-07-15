#include "http/HttpHandler.h"

#include "http/HttpSessionBase.h"
#include "router/RxRouter.h"
#include "util/Logger.h"

#include <utility>

namespace pz::http
{

void HttpHandler::setRxRouter(pz::router::RxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

SessionId HttpHandler::addSession(std::shared_ptr<HttpSessionBase> session)
{
    const SessionId id = ++m_nextId;  // 0 stays reserved
    session->setId(id);
    m_sessions.emplace(id, std::move(session));
    return id;
}

void HttpHandler::removeSession(SessionId id)
{
    m_sessions.erase(id);
}

void HttpHandler::ingress(HttpRequest request, SessionId id)
{
    if (!m_rxRouter)
    {
        LOG_ERROR("rx router is not initialized");
        egress(HttpResponse{503, "application/json; charset=utf-8",
                            R"({"error":"unavailable"})"},
               id);
        return;
    }

    m_rxRouter->handleHttpMessage(std::move(request), id);
}

void HttpHandler::egress(HttpResponse response, SessionId id)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end())
    {
        LOG_TRACE("egress: session already gone (id={})", id);
        return;
    }

    // Copy the shared_ptr so the session outlives the send() call even if send() (via a write
    // error -> removeSession) drops the registry's reference synchronously.
    std::shared_ptr<HttpSessionBase> session = it->second;
    session->send(std::move(response));
}

} // namespace pz::http
