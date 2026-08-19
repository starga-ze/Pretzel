#include "service/web/WebGrpcEvent.h"

#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace pz::mgmtd
{

WebGrpcEventType webGrpcEventFor(GrpcCmd cmd) noexcept
{
    switch (cmd)
    {
    case GrpcCmd::Chat:          return WebGrpcEventType::ChatResponse;
    case GrpcCmd::CorpusCheck:   return WebGrpcEventType::CorpusCheckResponse;
    case GrpcCmd::CorpusStatus:  return WebGrpcEventType::CorpusStatusResponse;
    case GrpcCmd::CorpusRefresh: return WebGrpcEventType::CorpusRefreshProgress;
    case GrpcCmd::Unknown:       break;
    }
    return WebGrpcEventType::Unknown;
}

WebGrpcEvent::WebGrpcEvent(WebGrpcEventType type, std::uint32_t ticket, std::string json)
    : MgmtdEvent(MgmtdEventDomain::Web), m_type(type), m_ticket(ticket), m_json(std::move(json))
{
}

WebGrpcEventType WebGrpcEvent::type() const
{
    return m_type;
}

std::uint32_t WebGrpcEvent::ticket() const
{
    return m_ticket;
}

const std::string& WebGrpcEvent::json() const
{
    return m_json;
}

void WebGrpcEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    switch (m_type)
    {
    // Unary answers resolve the ticket the browser is polling. Chat, check and status share the
    // one ticket store: all three are "one JSON document, collected once by whoever asked".
    case WebGrpcEventType::ChatResponse:
    case WebGrpcEventType::CorpusCheckResponse:
    case WebGrpcEventType::CorpusStatusResponse:
        serviceManager.setChatResult(m_ticket, std::move(m_json));
        return;

    // A refresh reports many times and is read by any number of polls, so it overwrites one live
    // slot rather than resolving a ticket. The service manager is what decides the run is over —
    // the transport reports progress, it does not get to declare state.
    case WebGrpcEventType::CorpusRefreshProgress:
    {
        // Reading `final` is this handler's job, not the transport's. A stream that ends without
        // it — the process died mid-crawl — leaves the run marked running forever, so a message
        // that will not parse is treated as terminal: a refresh nobody can describe is over.
        const nlohmann::json body = nlohmann::json::parse(m_json, nullptr, false);
        const bool finished = body.is_discarded() || body.value("final", false);
        if (body.is_discarded())
        {
            LOG_WARN("unparseable refresh progress from pretzel-ai; ending the run");
        }
        serviceManager.setCorpusProgress(std::move(m_json), finished);
        return;
    }

    case WebGrpcEventType::Unknown:
        break;
    }

    LOG_WARN("dropping pretzel-ai answer with no handler (type={}, ticket={})",
             static_cast<std::uint32_t>(m_type), m_ticket);
}

}
