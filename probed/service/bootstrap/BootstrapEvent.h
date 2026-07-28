#pragma once

#include "event/ProbedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::probed
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    ReceiveServerHello = 2,
    SendRuntimeReady = 3,
    ReceiveRuntimeStart = 4
};

class BootstrapEvent final : public ProbedEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(ProbedServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
    BootstrapEventType m_type{BootstrapEventType::Unknown};
};

}
