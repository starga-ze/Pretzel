#pragma once

#include "event/CollectordEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    ReceiveServerHello = 2,
    SendRuntimeReady = 3,
    ReceiveRuntimeStart = 4
};

class BootstrapEvent final : public CollectordEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(CollectordServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
