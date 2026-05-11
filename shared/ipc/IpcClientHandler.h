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
    void onMessage(const IpcMessage& msg) override;
};

} // namespace nf::ipc
