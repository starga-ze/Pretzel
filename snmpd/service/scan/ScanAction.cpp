#include "service/scan/ScanAction.h"
#include "service/SnmpdServiceManager.h"

namespace pz::snmpd
{

ScanAction::ScanAction(ScanActionType type, std::string resultJson)
    : SnmpdAction(SnmpdActionDomain::Scan),
      m_type(type),
      m_resultJson(std::move(resultJson))
{
}

void ScanAction::dispatch(SnmpdServiceManager& serviceManager)
{
    serviceManager.scanService().handleAction(serviceManager, *this);
}

ScanActionType ScanAction::type() const
{
    return m_type;
}

const std::string& ScanAction::resultJson() const
{
    return m_resultJson;
}

} // namespace pz::snmpd
