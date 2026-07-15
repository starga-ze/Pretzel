#pragma once

#include "event/MgmtdEvent.h"

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class WebEvent final : public MgmtdEvent
{
public:
    WebEvent(pz::http::HttpRequest request, pz::http::SessionId id);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    const pz::http::HttpRequest& request() const;
    pz::http::SessionId sessionId() const;

private:
    pz::http::HttpRequest m_request;
    pz::http::SessionId m_sessionId;
};

}
