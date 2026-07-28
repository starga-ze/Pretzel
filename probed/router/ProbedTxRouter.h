#pragma once

#include "icmp/IcmpEngineHandler.h"
#include "icmp/IcmpPacket.h"
#include "ipc/IpcClientHandler.h"
#include "router/TxRouter.h"

namespace pz::probed
{

class ProbedTxRouter : public pz::router::TxRouter
{
public:
    ProbedTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, IcmpEngineHandler* icmpEngineHandler);
    ~ProbedTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    void handleIcmpPacket(std::unique_ptr<IcmpPacket> packet, std::string dstIp);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
    IcmpEngineHandler* m_icmpEngineHandler;
};

}
