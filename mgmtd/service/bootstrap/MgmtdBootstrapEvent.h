#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

enum class MgmtdBootstrapEventType : std::uint32_t
{
    Unknown            = 0,
    SendClientHello    = 1,
    ReceiveServerHello = 2,
    SendRuntimeReady   = 3,
    ReceiveRuntimeStart = 4
};

class MgmtdBootstrapEvent final : public MgmtdEvent
{
public:
    explicit MgmtdBootstrapEvent(MgmtdBootstrapEventType type);

    MgmtdBootstrapEvent(MgmtdBootstrapEventType type,
                        std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    MgmtdBootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    MgmtdBootstrapEventType m_type{MgmtdBootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::mgmtd
