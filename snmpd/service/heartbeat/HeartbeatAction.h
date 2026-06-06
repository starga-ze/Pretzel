#pragma once

#include "action/SnmpdAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>

namespace nf::snmpd
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown               = 0,
    SendHeartbeatResponse = 1
};

class HeartbeatAction final : public SnmpdAction
{
public:
    HeartbeatAction(HeartbeatActionType type, nf::ipc::IpcDaemon dst);

    HeartbeatActionType type() const;
    nf::ipc::IpcDaemon dst() const;

    void dispatch(SnmpdServiceManager& serviceManager) override;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    nf::ipc::IpcDaemon m_dst{nf::ipc::IpcDaemon::Unknown};
};

} // namespace nf::snmpd
