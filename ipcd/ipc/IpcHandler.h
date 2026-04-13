#pragma once

#include "config/ConfigTypes.h"
#include "io/Epoll.h"
#include "ipc/IpcConnection.h"
#include "socket/UnixDomainSocket.h"

#include <memory>
#include <unordered_map>

namespace nf::ipcd
{

class UnixDomainSocket;

class IpcHandler
{
public:
    explicit IpcHandler(const nf::config::IpcConfig& cfg);

    void handleAccept(
            nf::socket::UnixDomainSocket& listener,
            std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
            nf::io::Epoll& epoll);

    void handleRecv(
            int fd,
            std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
            nf::io::Epoll& epoll);

    void handleSend(
            int fd,
            std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
            nf::io::Epoll& epoll);

    void closeConnection(
            int fd,
            std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
            nf::io::Epoll& epoll);

private:
    nf::config::IpcConfig m_cfg;
};

} // namespace nf::ipcd
