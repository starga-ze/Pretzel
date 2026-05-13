#pragma once

#include "router/RxRouter.h"

#include "router/EnginedTxRouter.h"

namespace nf::engined
{

class EnginedRxRouter : public nf::router::RxRouter
{
public:
    EnginedRxRouter(EnginedTxRouter* txRouter);
    ~EnginedRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    EnginedTxRouter* m_txRouter;
};
} // namespace nf::engined
