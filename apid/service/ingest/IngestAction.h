#pragma once

#include "action/ApidAction.h"

#include "http/HttpMessage.h"

namespace pz::apid
{

class IngestAction final : public ApidAction
{
public:
    IngestAction(pz::http::HttpResponse response, pz::http::SessionId id);

    void dispatch(ApidServiceManager& serviceManager) override;

    pz::http::HttpResponse& response();
    pz::http::SessionId sessionId() const;

private:
    pz::http::HttpResponse m_response;
    pz::http::SessionId m_sessionId;
};

}
