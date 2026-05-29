#pragma once

#include "socket/UnixDomainSocket.h"
#include "config/ConfigTypes.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcHandler.h"

#include "router/RxRouter.h"
#include "router/TxRouter.h"

#include <memory>
#include <unordered_map>

namespace nf::ipcd
{

class IpcServer;

struct RuntimeState
{
    int fd {-1};
    bool ready {false};
};

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

    void setRxRouter(nf::router::RxRouter* rxRouter);

    void markRuntimeReady(nf::ipc::IpcDaemon daemon, bool ready = true);
    const std::unordered_map<nf::ipc::IpcDaemon, RuntimeState>& getRuntimeTable() const;

private:
    bool bindRoute(nf::ipc::IpcDaemon daemon, int fd);
    int findRoute(nf::ipc::IpcDaemon daemon) const;
    void removeRoute(int fd);

    void unicast(std::unique_ptr<nf::ipc::IpcMessage> msg);
    void broadcast(std::unique_ptr<nf::ipc::IpcMessage> msg);
    bool sendFrame(int fd, const std::unique_ptr<nf::ipc::IpcMessage>& msg);

    IpcServer* m_ipcServer = nullptr;
    nf::router::RxRouter* m_rxRouter = nullptr;

    nf::config::IpcConfig m_cfg;

    std::unordered_map<nf::ipc::IpcDaemon, RuntimeState> m_runtimeTable;
};

} // namespace nf::ipcd
