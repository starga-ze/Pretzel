#pragma once

#include "router/RxRouter.h"

#include "router/IpcdTxRouter.h"

namespace nf::ipcd
{

class IpcdRxRouter : public nf::router::RxRouter
{
public:
    IpcdRxRouter(IpcdTxRouter* txRouter);
    ~IpcdRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    IpcdTxRouter* m_txRouter;
};

} // namespace nf::ipcd
