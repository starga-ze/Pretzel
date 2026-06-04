#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::action
{

template <typename ActionT, typename DomainT> class ActionFactory
{
public:
    ActionFactory() = default;
    virtual ~ActionFactory() = default;

    virtual std::unique_ptr<ActionT> create(DomainT domain, std::uint32_t type) = 0;
};

} // namespace nf::action
