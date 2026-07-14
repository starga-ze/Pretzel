#include "http/MgmtdHttpHandler.h"

#include "router/MgmtdRxRouter.h"

#include "util/Logger.h"

#include <utility>

namespace pz::mgmtd
{

MgmtdHttpHandler::MgmtdHttpHandler(MgmtdRxRouter* rxRouter)
    : m_rxRouter(rxRouter)
{
}

void MgmtdHttpHandler::handle(pz::http::HttpRequest request,
                              std::shared_ptr<pz::http::HttpResponder> responder)
{
    if (!m_rxRouter)
    {
        LOG_ERROR("rx router is not initialized");
        pz::http::HttpResponse res;
        res.status = 503;
        res.body   = R"({"error":"unavailable"})";
        responder->send(std::move(res));
        return;
    }

    m_rxRouter->dispatchHttp(std::move(request), std::move(responder));
}

} // namespace pz::mgmtd
