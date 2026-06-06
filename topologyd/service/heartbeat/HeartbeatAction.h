#pragma once

#include "action/TopologydAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>

namespace nf::topologyd
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown               = 0,
    SendHeartbeatResponse = 1
};

class HeartbeatAction final : public TopologydAction
{
public:
    HeartbeatAction(HeartbeatActionType type, nf::ipc::IpcDaemon dst);

    HeartbeatActionType type() const;
    nf::ipc::IpcDaemon dst() const;

    void dispatch(TopologydServiceManager& serviceManager) override;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    nf::ipc::IpcDaemon  m_dst{nf::ipc::IpcDaemon::Unknown};
};

} // namespace nf::topologyd
