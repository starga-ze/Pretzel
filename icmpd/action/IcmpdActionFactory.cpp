#include "action/IcmpdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace nf::icmpd
{

std::unique_ptr<IcmpdAction> IcmpdActionFactory::create(std::unique_ptr<nf::ipc::IpcMessage> message)
{
    if (!message)
    {
        LOG_WARN("IcmpdActionFactory: empty IpcMessage");
        return nullptr;
    }

    return nullptr;
}

std::unique_ptr<IcmpdAction> IcmpdActionFactory::create(IcmpdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IcmpdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("Unhandled action domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::icmpd
