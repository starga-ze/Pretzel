#pragma once

#include "action/CollectordAction.h"

#include <cstdint>

namespace pz::collectord
{

enum class ApiActionType : std::uint32_t
{
    Unknown = 0,
    RequestKeys = 1,   // ask engined for the issued keys (ApiCredentialStateRequest)
};

// Side effects the Api service returns from an event (event in → action out → IPC on handleAction),
// the same shape heartbeat uses. Keeps the outbound send out of the scheduling/handler logic.
class ApiAction final : public CollectordAction
{
public:
    explicit ApiAction(ApiActionType type);

    ApiActionType type() const;

    void dispatch(CollectordServiceManager& serviceManager) override;

private:
    ApiActionType m_type{ApiActionType::Unknown};
};

}
