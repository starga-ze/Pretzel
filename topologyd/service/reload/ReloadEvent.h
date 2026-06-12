#pragma once

#include "event/TopologydEvent.h"

#include <cstdint>

namespace pz::topologyd
{

enum class ReloadEventType : std::uint32_t
{
    Unknown             = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public TopologydEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(TopologydServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

} // namespace pz::topologyd
