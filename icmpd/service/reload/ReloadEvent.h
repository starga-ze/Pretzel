#pragma once

#include "event/IcmpdEvent.h"

#include <cstdint>

namespace pz::icmpd
{

enum class ReloadEventType : std::uint32_t
{
    Unknown             = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public IcmpdEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(IcmpdServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

} // namespace pz::icmpd
