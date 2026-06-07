#pragma once

#include "router/RxRouter.h"
#include "event/MgmtdEvent.h"
#include "event/MgmtdEventFactory.h"
#include "service/MgmtdServiceManager.h"

namespace pz::mgmtd
{

class MgmtdRxRouter : public pz::router::RxRouter
{
public:
    MgmtdRxRouter(MgmtdEventFactory* eventFactory,
                  MgmtdServiceManager* serviceManager);
    ~MgmtdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::mgmtd
