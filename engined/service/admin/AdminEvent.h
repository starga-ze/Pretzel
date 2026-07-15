#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class AdminEventType : std::uint32_t
{
    Unknown = 0,
    ReceivePasswordUpdate = 1,
};

class AdminEvent final : public EnginedEvent
{
public:
    explicit AdminEvent(AdminEventType type);
    AdminEvent(AdminEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    AdminEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    AdminEventType m_type{AdminEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
