#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

enum class MgmtdProbeEventType : std::uint32_t
{
    Unknown            = 0,
    ReceiveProbeResult = 1
};

class MgmtdProbeEvent final : public MgmtdEvent
{
public:
    explicit MgmtdProbeEvent(MgmtdProbeEventType type);

    MgmtdProbeEvent(MgmtdProbeEventType type,
                    std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    MgmtdProbeEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    MgmtdProbeEventType m_type{MgmtdProbeEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::mgmtd
