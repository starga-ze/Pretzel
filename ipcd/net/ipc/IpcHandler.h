#pragma once

#include "config/ConfigTypes.h"
#include "io/Epoll.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace nf::shared::io
{
class Epoll;
}

namespace nf::ipcd
{

class UnixDomainSocket;
class IpcConnection;

class IpcHandler
{
public:
    explicit IpcHandler(const nf::config::IpcConfig& cfg);

    void handleAccept(
            UnixDomainSocket& listener,
            std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
            nf::io::Epoll& epoll);

    void handleReadable(
            int fd,
            std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
            nf::io::Epoll& epoll);

    void handleWritable(
            int fd,
            std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
            nf::io::Epoll& epoll);

private:
    void closeConnection(
            int fd,
            std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
            nf::io::Epoll& epoll);

private:
    nf::config::IpcConfig m_cfg;
};

} // namespace nf::ipcd
