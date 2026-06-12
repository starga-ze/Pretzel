#pragma once

#include "event/SnmpdEvent.h"

#include <cstdint>

namespace pz::snmpd
{

enum class ReloadEventType : std::uint32_t
{
    Unknown             = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public SnmpdEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(SnmpdServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

} // namespace pz::snmpd
