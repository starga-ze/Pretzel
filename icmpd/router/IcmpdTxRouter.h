#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "icmp/IcmpEngineHandler.h"
#include "icmp/IcmpPacket.h"

namespace pz::icmpd
{

class IcmpdTxRouter : public pz::router::TxRouter
{
public:
    IcmpdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, IcmpEngineHandler* icmpEngineHandler);
    ~IcmpdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    void handleIcmpPacket(std::unique_ptr<IcmpPacket> packet, std::string dstIp);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
    IcmpEngineHandler* m_icmpEngineHandler;
};

} // namespace pz::icmpd
