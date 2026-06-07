#pragma once

#include "event/TopologydEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::topologyd
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown             = 0,
    SendClientHello     = 1,
    ReceiveServerHello  = 2,
    SendRuntimeReady    = 3,
    ReceiveRuntimeStart = 4
};

class BootstrapEvent final : public TopologydEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type,
                   std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(TopologydServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::topologyd
