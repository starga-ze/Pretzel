#pragma once

#include "event/ProbedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::probed
{

enum class ApiEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveConnectorTestRequest = 1,
    ReceiveKeyState = 2,
};

class ApiEvent final : public ProbedEvent
{
public:
    explicit ApiEvent(ApiEventType type);

    ApiEvent(ApiEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(ProbedServiceManager& serviceManager) override;

    ApiEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    ApiEventType m_type{ApiEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
