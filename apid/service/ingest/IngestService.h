#pragma once

#include "http/HttpMessage.h"

#include <string>

namespace pz::apid
{

class ApidServiceManager;
class IngestEvent;
class IngestAction;

class IngestService
{
public:
    IngestService();

    void handleEvent(ApidServiceManager& serviceManager, const IngestEvent& event);

    void handleAction(ApidServiceManager& serviceManager, IngestAction& action);

private:
    void route(const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    std::string bearerToken(const std::string& authorization) const;

    std::string m_ingestToken;
};

}
