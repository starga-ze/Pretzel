#pragma once

#include "action/MgmtdAction.h"

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class WebAction final : public MgmtdAction
{
public:
    WebAction(pz::http::HttpResponse response, pz::http::SessionId id);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    pz::http::HttpResponse& response();
    pz::http::SessionId sessionId() const;

private:
    pz::http::HttpResponse m_response;
    pz::http::SessionId m_sessionId;
};

}
