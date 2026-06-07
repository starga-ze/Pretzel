#pragma once

#include "action/EnginedAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>
#include <string>

namespace pz::engined
{

enum class HeartbeatActionType : std::uint32_t
{
    Unknown              = 0,
    SendHeartbeatRequest = 1,
    SendHeartbeatResult  = 2
};

class HeartbeatAction final : public EnginedAction
{
public:
    explicit HeartbeatAction(HeartbeatActionType type);

    HeartbeatAction(HeartbeatActionType type,
                    pz::ipc::IpcDaemon dst);

    HeartbeatAction(HeartbeatActionType type,
                    std::string resultJson);

    void dispatch(EnginedServiceManager& serviceManager) override;

    HeartbeatActionType type() const;
    pz::ipc::IpcDaemon dst() const;
    const std::string& resultJson() const;

private:
    HeartbeatActionType m_type{HeartbeatActionType::Unknown};
    pz::ipc::IpcDaemon  m_dst{pz::ipc::IpcDaemon::Unknown};
    std::string         m_resultJson;
};

} // namespace pz::engined
