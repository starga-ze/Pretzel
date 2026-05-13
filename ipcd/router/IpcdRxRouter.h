#pragma once

#include "router/RxRouter.h"

namespace nf::ipcd
{

class IpcdRxRouter : public nf::router::RxRouter
{
public:
    IpcdRxRouter() = default;
    ~IpcdRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
};

} // namespace nf::ipcd
