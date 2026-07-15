#pragma once

#include "api/ApiTypes.h"
#include "snmp/SnmpTypes.h"

#include <map>
#include <string>
#include <vector>

namespace pz::scand
{

class ScandRxRouter;
class ApiEngine;

class ApiEngineHandler final
{
public:
    explicit ApiEngineHandler(ApiEngine* apiEngine);

    void egress(std::map<std::string, ApiCredential> devices);

    void onScanComplete(std::vector<SnmpDevice> devices);

    void setRxRouter(ScandRxRouter* rxRouter);

private:
    ApiEngine* m_apiEngine{nullptr};
    ScandRxRouter* m_rxRouter{nullptr};
};

}
