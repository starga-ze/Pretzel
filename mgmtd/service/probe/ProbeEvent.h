#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

enum class ProbeEventType : std::uint32_t
{
    Unknown            = 0,
    ReceiveProbeResult = 1
};

class ProbeEvent final : public MgmtdEvent
{
public:
    explicit ProbeEvent(ProbeEventType type);

    ProbeEvent(ProbeEventType type,
                    std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    ProbeEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    ProbeEventType m_type{ProbeEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::mgmtd
