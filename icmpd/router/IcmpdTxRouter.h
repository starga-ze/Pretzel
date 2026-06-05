#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "icmp/IcmpEngineHandler.h"
#include "icmp/IcmpPacket.h"

namespace nf::icmpd
{

class IcmpdTxRouter : public nf::router::TxRouter
{
public:
    IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler, IcmpEngineHandler* icmpEngineHandler);
    ~IcmpdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    void handleIcmpPacket(std::unique_ptr<IcmpPacket> packet, std::string dstIp);

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
    IcmpEngineHandler* m_icmpEngineHandler;
};

} // namespace nf::icmpd
