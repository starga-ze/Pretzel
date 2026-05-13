#pragma once

#include "socket/UnixDomainSocket.h"
#include "config/ConfigTypes.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcHandler.h"

#include "router/IpcdRxRouter.h"
#include "router/IpcdTxRouter.h"

#include <memory>
#include <unordered_map>

namespace nf::ipcd
{

class IpcServer;

class IpcServerHandler : public nf::ipc::IpcHandler
{
public:
    explicit IpcServerHandler(IpcServer* ipcServer, const nf::config::IpcConfig& cfg);
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

    bool ingress(int fd, nf::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    void bindRoute(nf::ipc::IpcDaemon daemon, int fd);
    int findRoute(nf::ipc::IpcDaemon daemon) const;
    void removeRoute(int fd);

    IpcServer* m_ipcServer = nullptr;

    std::unique_ptr<IpcdTxRouter> m_txRouter = nullptr;
    std::unique_ptr<IpcdRxRouter> m_rxRouter = nullptr;

    nf::config::IpcConfig m_cfg;

    std::unordered_map<nf::ipc::IpcDaemon, int> m_routeTable;
};

} // namespace nf::ipcd
