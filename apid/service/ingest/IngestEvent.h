#pragma once

#include "event/ApidEvent.h"

#include "http/HttpMessage.h"

namespace pz::apid
{

class IngestEvent final : public ApidEvent
{
public:
    IngestEvent(pz::http::HttpRequest request, pz::http::SessionId id);

    void dispatch(ApidServiceManager& serviceManager) override;

    const pz::http::HttpRequest& request() const;
    pz::http::SessionId sessionId() const;

private:
    pz::http::HttpRequest m_request;
    pz::http::SessionId m_sessionId;
};

}
