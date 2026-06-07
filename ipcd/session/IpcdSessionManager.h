#pragma once

#include "session/Session.h"
#include "session/SessionManager.h"

#include <unordered_map>
#include <memory>

namespace pz::ipcd
{

class IpcdSessionManager : public pz::session::SessionManager
{
public:
    IpcdSessionManager() = default;
    ~IpcdSessionManager() override = default;

    void handleMessage(const pz::ipc::IpcMessage& msg) override;

private:
    pz::session::Session* findSession(const std::string& sessionId);
    pz::session::Session* findSession(pz::ipc::IpcDaemon src, pz::ipc::IpcDaemon dst);

    pz::session::Session* createSession(pz::ipc::IpcDaemon src, pz::ipc::IpcDaemon dst);

    void removeSession(const std::string& sessionId);
    void clear();

    static std::string makePairKey(pz::ipc::IpcDaemon src, pz::ipc::IpcDaemon dst);
    static std::string generateSessionId();

    std::unordered_map<std::string, std::unique_ptr<pz::session::Session>> m_sessions;
    std::unordered_map<std::string, std::string> m_pairToSessionId;

};

} // namespace pz::ipcd
