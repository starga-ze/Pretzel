#pragma once

#include "event/TopologydEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::topologyd
{

enum class TopologyEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveRequest = 1,   // mgmtd asked for one site's composed picture
};

class TopologyEvent final : public TopologydEvent
{
public:
    explicit TopologyEvent(TopologyEventType type);
    TopologyEvent(TopologyEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(TopologydServiceManager& serviceManager) override;

    TopologyEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    TopologyEventType m_type{TopologyEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
