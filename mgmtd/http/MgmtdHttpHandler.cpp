#include "http/MgmtdHttpHandler.h"

#include "http/MgmtdHttpRxRouter.h"

#include "util/Logger.h"

#include <boost/beast/http.hpp>

namespace pz::mgmtd
{

namespace http = boost::beast::http;

MgmtdHttpHandler::MgmtdHttpHandler(MgmtdHttpRxRouter* rxRouter)
    : m_rxRouter(rxRouter)
{
}

MgmtdHttpHandler::Response MgmtdHttpHandler::handle(const Request& req)
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

    return m_rxRouter->dispatch(req);
}

} // namespace pz::mgmtd
