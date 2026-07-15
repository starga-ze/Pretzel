#pragma once

#include "event/AuthdEvent.h"

#include <cstdint>

namespace pz::authd
{

enum class ReloadEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public AuthdEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(AuthdServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

}
