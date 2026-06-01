#include "service/bootstrap/BootstrapService.h"

#include "util/Logger.h"

namespace nf::icmpd
{

BootstrapService::BootstrapService()
{
}

void BootstrapService::start()
{
    
}

std::unique_ptr<nf::event::Event> BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{

}

bool BootstrapService::isReady()
{
    return false;
}

} // namespace nf::icmpd
