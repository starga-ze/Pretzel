#include "service/IcmpdServiceManager.h"

#include "util/Logger.h"

#include <chrono> 

namespace nf::icmpd
{

IcmpdServiceManager::IcmpdServiceManager() :
    m_bootstrapService(std::make_unique<BootstrapService>())
{
}

void IcmpdServiceManager::start()
{
    m_bootstrapService->start();
}

void IcmpdServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        dispatch(m_bootstrapService->schedule(now));
        return;
    }

    //dispatch(m_probeService->schedule(now));
    //dispatch(m_scanService->schedule(now));
}

void IcmpdServiceManager::dispatch(std::unique_ptr<nf::event::Event> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void IcmpdServiceManager::execute()
{
   while (!m_eventQueue.empty())
   {
       auto event = std::move(m_eventQueue.front());
       m_eventQueue.pop();

       auto* icmpdEvent = dynamic_cast<IcmpdEvent*>(event.get());

       if (!icmpdEvent)
       {
           LOG_WARN("Unknown event");
           continue;
       }

       switch(icmpdEvent->domain())
       {
           case IcmpdEventDomain::Bootstrap:
               m_bootstrapService->handleEvent(*icmpdEvent);
               break;

           default:
               LOG_WARN("Unhandled icmpd event domain={}", static_cast<std::uint32_t>(icmpdEvent->domain()));
               break;
       }

       // event->execute();
   }
}


} // namespace nf::icmpd
