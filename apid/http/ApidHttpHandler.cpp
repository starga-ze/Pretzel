#include "http/ApidHttpHandler.h"

#include "router/ApidRxRouter.h"

#include "http/HttpExchange.h"
#include "util/Logger.h"

#include <boost/beast/http.hpp>

namespace pz::apid
{

namespace http = boost::beast::http;

ApidHttpHandler::ApidHttpHandler(ApidRxRouter* rxRouter)
    : m_rxRouter(rxRouter)
{
}

ApidHttpHandler::Response ApidHttpHandler::handle(const Request& req)
{
    if (!m_rxRouter)
    {
        LOG_ERROR("rx router is not initialized");
        Response res{http::status::service_unavailable, req.version()};
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.keep_alive(false);
        res.body() = R"({"error":"unavailable"})";
        res.prepare_payload();
        return res;
    }

    // beast -> transport-agnostic DTO. Everything past here (router/event/service) is
    // beast-free, mirroring how IPC ingress becomes an IpcMessage-derived event.
    pz::http::HttpRequestView view;
    view.method = std::string(req.method_string());
    view.target = std::string(req.target());
    if (auto it = req.find(http::field::authorization); it != req.end())
        view.authorization = std::string(it->value());
    view.body = req.body();

    const pz::http::HttpResponse out = m_rxRouter->dispatchHttp(view);

    // DTO -> beast.
    Response res{static_cast<http::status>(out.status), req.version()};
    res.set(http::field::server, "pz-apid");
    res.set(http::field::content_type, out.contentType);
    res.keep_alive(req.keep_alive());
    res.body() = out.body;
    res.prepare_payload();
    return res;
}

} // namespace pz::apid
