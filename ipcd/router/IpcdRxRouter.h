#pragma once

#include "event/IpcdEvent.h"
#include "event/IpcdEventFactory.h"
#include "router/IpcdTxRouter.h"
#include "router/RxRouter.h"
#include "service/IpcdServiceManager.h"

namespace pz::ipcd
{

class IpcdRxRouter : public pz::router::RxRouter
{
public:
    IpcdRxRouter(IpcdEventFactory* eventFactory, IpcdServiceManager* serviceManager, IpcdTxRouter* txRouter);
    ~IpcdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    IpcdEventFactory* m_eventFactory{nullptr};
    IpcdServiceManager* m_serviceManager{nullptr};
    IpcdTxRouter* m_txRouter{nullptr};
};

}
