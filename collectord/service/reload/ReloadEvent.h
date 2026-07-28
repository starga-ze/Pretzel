#pragma once

#include "event/CollectordEvent.h"

#include <cstdint>

namespace pz::collectord
{

enum class ReloadEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveConfigReload = 1,
};

class ReloadEvent final : public CollectordEvent
{
public:
    explicit ReloadEvent(ReloadEventType type);

    void dispatch(CollectordServiceManager& serviceManager) override;

    ReloadEventType type() const;

private:
    ReloadEventType m_type{ReloadEventType::Unknown};
};

}
