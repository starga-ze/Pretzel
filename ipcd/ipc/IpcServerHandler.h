#pragma once

#include "config/ConfigTypes.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcHandler.h"
#include "socket/UnixDomainSocket.h"

#include "router/RxRouter.h"
#include "router/TxRouter.h"

#include <memory>
#include <unordered_map>

namespace pz::ipcd
{

class IpcServer;

struct RuntimeState
{
    int fd{-1};
    bool ready{false};
    uint32_t generation{0};
    uint64_t appliedVersion{0};
};

class IpcServerHandler : public pz::ipc::IpcHandler
{
public:
    explicit IpcServerHandler(IpcServer* ipcServer, const pz::config::IpcConfig& cfg);
    ~IpcServerHandler() override = default;

    void handleAccept(pz::socket::UnixDomainSocket& listener,
                      std::unordered_map<int, std::unique_ptr<pz::ipc::IpcConnection>>& connections,
                      pz::io::Epoll& epoll);

    bool handleRecv(int fd, std::unordered_map<int, std::unique_ptr<pz::ipc::IpcConnection>>& connections,
                    pz::io::Epoll& epoll);

    bool handleSend(int fd, std::unordered_map<int, std::unique_ptr<pz::ipc::IpcConnection>>& connections,
                    pz::io::Epoll& epoll);

    void closeConnection(int fd, std::unordered_map<int, std::unique_ptr<pz::ipc::IpcConnection>>& connections,
                         pz::io::Epoll& epoll);

    bool ingress(int fd, pz::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setRxRouter(pz::router::RxRouter* rxRouter);

    void markRuntimeReady(pz::ipc::IpcDaemon daemon, bool ready = true, uint64_t appliedVersion = 0);
    const std::unordered_map<pz::ipc::IpcDaemon, RuntimeState>& getRuntimeTable() const;

private:
    bool bindRoute(pz::ipc::IpcDaemon daemon, int fd);
    int findRoute(pz::ipc::IpcDaemon daemon) const;
    void removeRoute(int fd);

    void unicast(std::unique_ptr<pz::ipc::IpcMessage> msg);
    void broadcast(std::unique_ptr<pz::ipc::IpcMessage> msg);
    bool sendFrame(int fd, const std::unique_ptr<pz::ipc::IpcMessage>& msg);

    IpcServer* m_ipcServer = nullptr;
    pz::router::RxRouter* m_rxRouter = nullptr;

    pz::config::IpcConfig m_cfg;

    std::unordered_map<pz::ipc::IpcDaemon, RuntimeState> m_runtimeTable;
};

}
