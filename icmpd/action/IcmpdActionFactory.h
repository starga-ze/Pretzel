#pragma once

#include "action/ActionFactory.h"
#include "action/IcmpdAction.h"

#include <cstdint>
#include <memory>

namespace nf::icmpd
{

class IcmpdActionFactory final : public nf::action::ActionFactory<IcmpdAction, IcmpdActionDomain>
{
public:
    IcmpdActionFactory() = default;
    ~IcmpdActionFactory() override = default;

    std::unique_ptr<IcmpdAction> create(std::unique_ptr<nf::ipc::IpcMessage> message) override;

    std::unique_ptr<IcmpdAction> create(IcmpdActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::icmpd
