#pragma once

#include "action/ProbedAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>

namespace pz::probed
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown = 0,
    SendHeartbeatResponse = 1
};

class HeartbeatAction final : public ProbedAction
{
public:
    HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst);

    HeartbeatActionType type() const;
    pz::ipc::IpcDaemon dst() const;

    void dispatch(ProbedServiceManager& serviceManager) override;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    pz::ipc::IpcDaemon m_dst{pz::ipc::IpcDaemon::Unknown};
};

}
