#include "service/scan/ScanAction.h"
#include "service/ScandServiceManager.h"

namespace pz::scand
{

ScanAction::ScanAction(ScanActionType type, std::string resultJson)
    : ScandAction(ScandActionDomain::Scan),
      m_type(type),
      m_resultJson(std::move(resultJson))
{
}

void ScanAction::dispatch(ScandServiceManager& serviceManager)
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

} // namespace pz::scand
