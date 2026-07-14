#pragma once

#include "action/ApidAction.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::apid
{

// The HTTP analogue of an IPC egress action: it carries a produced response and the parked
// connection's responder. The service posts it after filling the response; when the
// ServiceManager drains it, dispatch() calls responder->send() to write the response back.
class IngestResponseAction final : public ApidAction
{
public:
    IngestResponseAction(std::shared_ptr<pz::http::HttpResponder> responder,
                       pz::http::HttpResponse response);

    void dispatch(ApidServiceManager& serviceManager) override;

private:
    std::shared_ptr<pz::http::HttpResponder> m_responder;
    pz::http::HttpResponse m_response;
};

} // namespace pz::apid
