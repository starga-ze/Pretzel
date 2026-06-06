#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown            = 0,
    SendClientHello    = 1,
    ReceiveServerHello = 2,
    SendRuntimeReady   = 3,
    ReceiveRuntimeStart = 4
};

class BootstrapEvent final : public MgmtdEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type,
                        std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::mgmtd
