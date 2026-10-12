#pragma once

#include "action/EnginedAction.h"

#include <cstdint>

namespace pz::engined
{

enum class CommitActionType : std::uint32_t
{
    Unknown = 0,
    ApplyCommit = 1,
};

class CommitAction final : public EnginedAction
{
public:
    explicit CommitAction(CommitActionType type);

    void dispatch(EnginedServiceManager& serviceManager) override;

    CommitActionType type() const;

private:
    CommitActionType m_type{CommitActionType::Unknown};
};

}
