#pragma once

#include "event/ProbedEvent.h"

#include <cstdint>

namespace pz::probed
{

enum class ReloadEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public ProbedEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(ProbedServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

}
