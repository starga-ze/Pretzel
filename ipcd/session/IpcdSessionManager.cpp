#include "session/IpcdSessionManager.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::ipcd
{

void IpcdSessionManager::handleMessage(int fd, const nf::ipc::IpcMessage& msg)
{
    nf::session::Session* session = findSession(fd);
    if (session == nullptr)
    {
        session = createSession(fd);
        if (session == nullptr)
        {
            LOG_ERROR("Failed to create session, fd={}", fd);
            return;
        }

        LOG_INFO("Session created, fd={}, sessionId={}", fd, session->getId());
    }

    LOG_INFO("Session find, try create Event");

    /* TODO: implement EventFactory + ServiceManager */

    // std::unique_ptr<Event> event = EventFactory::create(msg);

    // m_serviceManager.dispatch(std::move(event));
}

nf::session::Session* IpcdSessionManager::findSession(int fd)
{
    auto it = m_sessions.find(fd);
    if (it == m_sessions.end())
        return nullptr;

    return it->second.get();
}

const nf::session::Session* IpcdSessionManager::findSession(int fd) const
{
    auto it = m_sessions.find(fd);
    if (it == m_sessions.end())
        return nullptr;

    return it->second.get();
}

nf::session::Session* IpcdSessionManager::createSession(int fd)
{
    auto [it, inserted] = m_sessions.emplace(fd, std::make_unique<nf::session::Session>(fd));

    if (!inserted && !it->second)
        it->second = std::make_unique<nf::session::Session>(fd);

    return it->second.get();
}

}
