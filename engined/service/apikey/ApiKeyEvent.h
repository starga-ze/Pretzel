#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ApiKeyEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveStateUpdate = 1,
};

class ApiKeyEvent final : public EnginedEvent
{
public:
    explicit ApiKeyEvent(ApiKeyEventType type);
    ApiKeyEvent(ApiKeyEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    ApiKeyEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    ApiKeyEventType m_type{ApiKeyEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
