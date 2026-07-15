#pragma once

#include "action/MgmtdAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>

namespace pz::mgmtd
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown = 0,
    SendHeartbeatResponse = 1
};

class HeartbeatAction final : public MgmtdAction
{
public:
    HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst);

    HeartbeatActionType type() const;
    pz::ipc::IpcDaemon dst() const;

    void dispatch(MgmtdServiceManager& serviceManager) override;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    pz::ipc::IpcDaemon m_dst{pz::ipc::IpcDaemon::Unknown};
};

}
