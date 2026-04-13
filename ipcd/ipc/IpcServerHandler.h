#pragma once

#include "config/ConfigTypes.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcHandler.h"
#include "socket/UnixDomainSocket.h"

#include <memory>
#include <unordered_map>

namespace nf::ipcd
{

class IpcServerHandler : public nf::ipc::IpcHandler
{
public:
    explicit IpcServerHandler(const nf::config::IpcConfig& cfg);
    ~IpcServerHandler() override = default;

    void handleAccept(
        nf::socket::UnixDomainSocket& listener,
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll);

    bool handleRecv(
        int fd,
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll);

    bool handleSend(
        int fd,
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll);

    void closeConnection(
        int fd,
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll);

protected:
    void onMessage(int fd, const nf::ipc::IpcMessage& msg) override;

private:
    nf::config::IpcConfig m_cfg;
};

} // namespace nf::ipcd
