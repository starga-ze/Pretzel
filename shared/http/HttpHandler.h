#pragma once

#include "http/HttpMessage.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace pz::router
{
class RxRouter;
}

namespace pz::http
{

class HttpServerSessionBase;

class HttpHandler
{
public:
    HttpHandler() = default;

    void setRxRouter(pz::router::RxRouter* rxRouter);

    SessionId addSession(std::shared_ptr<HttpServerSessionBase> session);
    void removeSession(SessionId id);

    void ingress(HttpRequest request, SessionId id);

    void egress(HttpResponse response, SessionId id);

private:
    pz::router::RxRouter* m_rxRouter{nullptr};

    std::unordered_map<SessionId, std::shared_ptr<HttpServerSessionBase>> m_sessions;
    SessionId m_nextId{0};
};

}
