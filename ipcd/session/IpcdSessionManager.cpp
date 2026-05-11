#include "session/IpcdSessionManager.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <array>
#include <iomanip>
#include <sstream>

#include <openssl/rand.h>

namespace nf::ipcd
{

void IpcdSessionManager::handleMessage(const nf::ipc::IpcMessage& msg)
{
    nf::session::Session* session = findSession(msg.getSrc(), msg.getDst());

    if (session == nullptr)
    {
        if (msg.getCmd() != nf::ipc::IpcCmd::ClientHello)
        {
            LOG_WARN("Session not found. first message must be ClientHello, src={}, dst={}, cmd={}",
                     nf::ipc::IpcProtocol::daemonToStr(msg.getSrc()),
                     nf::ipc::IpcProtocol::daemonToStr(msg.getDst()),
                     nf::ipc::IpcProtocol::cmdToStr(msg.getCmd()));
            return;
        }

        session = createSession(msg.getSrc(), msg.getDst());
        if (session == nullptr)
        {
            LOG_ERROR("Failed to create session, src={}, dst={}",
                      nf::ipc::IpcProtocol::daemonToStr(msg.getSrc()),
                      nf::ipc::IpcProtocol::daemonToStr(msg.getDst()));
            return;
        }

        LOG_INFO("Session created, sessionId={}, src={}, dst={}",
                 session->getId(),
                 nf::ipc::IpcProtocol::daemonToStr(msg.getSrc()),
                 nf::ipc::IpcProtocol::daemonToStr(msg.getDst()));
    }
    else
    {
        if (msg.getCmd() == nf::ipc::IpcCmd::ClientHello)
        {
            LOG_WARN("Session already exists, ClientHello message will be drop, src={}, dst={}, cmd={}",
                    nf::ipc::IpcProtocol::daemonToStr(msg.getSrc()),
                    nf::ipc::IpcProtocol::daemonToStr(msg.getDst()),
                    nf::ipc::IpcProtocol::cmdToStr(msg.getCmd()));
            return;
        }
    }

    LOG_INFO("Session found, sessionId={}, src={}, dst={}, cmd={}",
             session->getId(),
             nf::ipc::IpcProtocol::daemonToStr(msg.getSrc()),
             nf::ipc::IpcProtocol::daemonToStr(msg.getDst()),
             nf::ipc::IpcProtocol::cmdToStr(msg.getCmd()));

    /*
     * TODO:
     *
     * std::unique_ptr<Event> event = EventFactory::create(*session, msg);
     * m_serviceManager.dispatch(std::move(event));
     */

    /* Temporaliy Implemented */

}

nf::session::Session* IpcdSessionManager::findSession(const std::string& sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return nullptr;

    return it->second.get();
}

nf::session::Session* IpcdSessionManager::findSession(
    nf::ipc::IpcDaemon src,
    nf::ipc::IpcDaemon dst)
{
    const auto pairKey = makePairKey(src, dst);

    auto it = m_pairToSessionId.find(pairKey);
    if (it == m_pairToSessionId.end())
        return nullptr;

    return findSession(it->second);
}

nf::session::Session* IpcdSessionManager::createSession(
    nf::ipc::IpcDaemon src,
    nf::ipc::IpcDaemon dst)
{
    const auto pairKey = makePairKey(src, dst);

    if (m_pairToSessionId.find(pairKey) != m_pairToSessionId.end())
    {
        LOG_WARN("Session pair already exists, src={}, dst={}",
                 nf::ipc::IpcProtocol::daemonToStr(src),
                 nf::ipc::IpcProtocol::daemonToStr(dst));
        return nullptr;
    }

    std::string sessionId = generateSessionId();
    if (sessionId.empty())
        return nullptr;

    while (m_sessions.find(sessionId) != m_sessions.end())
    {
        sessionId = generateSessionId();
        if (sessionId.empty())
            return nullptr;
    }

    auto session = std::make_unique<nf::session::Session>(
        sessionId,
        src,
        dst
    );

    nf::session::Session* raw = session.get();

    m_sessions.emplace(sessionId, std::move(session));
    m_pairToSessionId.emplace(pairKey, sessionId);

    return raw;
}

void IpcdSessionManager::removeSession(const std::string& sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;

    const auto pairKey = makePairKey(it->second->getSrc(), it->second->getDst());

    LOG_INFO("Session removed, sessionId={}, src={}, dst={}",
             it->second->getId(),
             nf::ipc::IpcProtocol::daemonToStr(it->second->getSrc()),
             nf::ipc::IpcProtocol::daemonToStr(it->second->getDst()));

    m_pairToSessionId.erase(pairKey);
    m_sessions.erase(it);
}

void IpcdSessionManager::clear()
{
    LOG_INFO("All ipcd sessions cleared, count={}", m_sessions.size());

    m_pairToSessionId.clear();
    m_sessions.clear();
}

std::string IpcdSessionManager::makePairKey(
    nf::ipc::IpcDaemon src,
    nf::ipc::IpcDaemon dst)
{
    return std::to_string(static_cast<int>(src)) + ":" +
           std::to_string(static_cast<int>(dst));
}

std::string IpcdSessionManager::generateSessionId()
{
    std::array<unsigned char, 32> bytes {};

    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        LOG_ERROR("RAND_bytes failed while generating sessionId");
        return {};
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (unsigned char b : bytes)
        oss << std::setw(2) << static_cast<int>(b);

    return oss.str();
}

}
