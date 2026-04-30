#pragma once

#include "session/Session.h"
#include "session/SessionManager.h"
#include "service/IpcdServiceManager.h"

#include <unordered_map>
#include <memory>

namespace nf::ipcd
{

class IpcdSessionManager : public nf::session::SessionManager
{
public:
    explicit IpcdSessionManager() = default;
    ~IpcdSessionManager() override = default;

    void handleMessage(int fd, const nf::ipc::IpcMessage& msg) override;

private:
    nf::session::Session* findSession(int fd);
    const nf::session::Session* findSession(int fd) const;
    nf::session::Session* createSession(int fd);

    IpcdServiceManager m_serviceManager;
    std::unordered_map<int, std::unique_ptr<nf::session::Session>> m_sessions;
};

} // namespace nf::ipcd
