#pragma once

#include "ipc/IpcHandler.h"

namespace nf::ipc
{

class IpcClientHandler : public IpcHandler
{
public:
    IpcClientHandler() = default;
    ~IpcClientHandler() override = default;

protected:
    bool ingress(int fd, nf::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<IpcMessage> msg) override;
};

} // namespace nf::ipc
