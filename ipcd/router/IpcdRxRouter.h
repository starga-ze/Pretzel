#pragma once

#include "router/RxRouter.h"
#include "event/IpcdEvent.h"
#include "event/IpcdEventFactory.h"
#include "service/IpcdServiceManager.h"
#include "router/IpcdTxRouter.h"

namespace nf::ipcd
{

class IpcdRxRouter : public nf::router::RxRouter
{
public:
    IpcdRxRouter(IpcdEventFactory* eventFactory,
                 IpcdServiceManager* serviceManager,
                 IpcdTxRouter* txRouter);
    ~IpcdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdServiceManager* m_serviceManager{nullptr};
    IpcdTxRouter* m_txRouter{nullptr};
};

} // namespace nf::ipcd
