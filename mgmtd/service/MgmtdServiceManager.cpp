#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

void MgmtdServiceManager::start()
{
    m_metricService.start();
}

void MgmtdServiceManager::schedule()
{
    m_metricService.tick(std::chrono::steady_clock::now());
}

void MgmtdServiceManager::execute()
{
}

AuthService& MgmtdServiceManager::authService()
{
    return m_authService;
}

MetricService& MgmtdServiceManager::metricService()
{
    return m_metricService;
}

} // namespace nf::mgmtd
