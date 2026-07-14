#pragma once

#include "action/MgmtdAction.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::mgmtd
{

// The HTTP analogue of an IPC egress action: it carries a produced response and the parked
// connection's responder. WebService posts it after filling the response; when the
// ServiceManager drains it, dispatch() calls responder->send() to write the response back.
class WebResponseAction final : public MgmtdAction
{
public:
    WebResponseAction(std::shared_ptr<pz::http::HttpResponder> responder,
                       pz::http::HttpResponse response);

    void dispatch(MgmtdServiceManager& serviceManager) override;

private:
    std::shared_ptr<pz::http::HttpResponder> m_responder;
    pz::http::HttpResponse m_response;
};

} // namespace pz::mgmtd
