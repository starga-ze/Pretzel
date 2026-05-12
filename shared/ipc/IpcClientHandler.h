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
    void onRxMessage(std::unique_ptr<IpcMessage> msg) override;
    void onTxMessage(std::unique_ptr<IpcMessage> msg) override;
};

} // namespace nf::ipc
