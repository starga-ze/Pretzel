#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

void MgmtdServiceManager::start()
{
    m_metricsService.start();
}

void MgmtdServiceManager::schedule()
{
    m_metricsService.tick(std::chrono::steady_clock::now());
}

void MgmtdServiceManager::execute()
{
}

AuthService& MgmtdServiceManager::authService()
{
    return m_authService;
}

MetricsService& MgmtdServiceManager::metricsService()
{
    return m_metricsService;
}

} // namespace nf::mgmtd
