#pragma once

#include "session/Session.h"
#include "session/SessionManager.h"

#include <unordered_map>
#include <memory>

namespace nf::ipcd
{

class IpcdSessionManager : public nf::session::SessionManager
{
public:
    IpcdSessionManager() = default;
    ~IpcdSessionManager() override = default;

    void handleMessage(const nf::ipc::IpcMessage& msg) override;

private:
    nf::session::Session* findSession(const std::string& sessionId);
    nf::session::Session* findSession(nf::ipc::IpcDaemon src, nf::ipc::IpcDaemon dst);

    nf::session::Session* createSession(nf::ipc::IpcDaemon src, nf::ipc::IpcDaemon dst);

    void removeSession(const std::string& sessionId);
    void clear();

    static std::string makePairKey(nf::ipc::IpcDaemon src, nf::ipc::IpcDaemon dst);
    static std::string generateSessionId();

    std::unordered_map<std::string, std::unique_ptr<nf::session::Session>> m_sessions;
    std::unordered_map<std::string, std::string> m_pairToSessionId;

};

} // namespace nf::ipcd
