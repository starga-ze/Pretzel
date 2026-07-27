#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ApiCredentialEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveStateUpdate = 1,
    ReceiveStateRequest = 2,
};

class ApiCredentialEvent final : public EnginedEvent
{
public:
    explicit ApiCredentialEvent(ApiCredentialEventType type);
    ApiCredentialEvent(ApiCredentialEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    ApiCredentialEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    ApiCredentialEventType m_type{ApiCredentialEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
