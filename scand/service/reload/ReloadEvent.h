#pragma once

#include "event/ScandEvent.h"

#include <cstdint>

namespace pz::scand
{

enum class ReloadEventType : std::uint32_t
{
    Unknown             = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public ScandEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(ScandServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

} // namespace pz::scand
