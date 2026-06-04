#pragma once

#include "event/IcmpdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::icmpd
{

enum class ProbeEventType : std::uint32_t
{
    Unknown = 0,
    StartProbe = 1
};

class ProbeEvent final : public IcmpdEvent
{
public:
    explicit ProbeEvent(ProbeEventType type);

    ProbeEvent(ProbeEventType type, std::unique_ptr<nf::ipc::IpcMessage> message);

    ProbeEventType type() const;

    void dispatch(IcmpdServiceManager& serviceManager) override;

private:
    ProbeEventType m_type{ProbeEventType::Unknown};
};
} // namespace nf::icmpd
