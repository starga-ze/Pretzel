#pragma once

#include "action/TopologydAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>

namespace pz::topologyd
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown = 0,
    SendHeartbeatResponse = 1
};

class HeartbeatAction final : public TopologydAction
{
public:
    HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst);

    HeartbeatActionType type() const;
    pz::ipc::IpcDaemon dst() const;

    void dispatch(TopologydServiceManager& serviceManager) override;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    pz::ipc::IpcDaemon m_dst{pz::ipc::IpcDaemon::Unknown};
};

}
