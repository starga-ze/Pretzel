#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::icmpd
{

enum class IcmpdActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
};

class IcmpdAction : public nf::action::Action
{
public:
    explicit IcmpdAction(IcmpdActionDomain domain);
    IcmpdAction(IcmpdActionDomain domain, std::unique_ptr<nf::ipc::IpcMessage> message);
    ~IcmpdAction() override = default;

    IcmpdActionDomain domain() const;

    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    IcmpdActionDomain m_domain{IcmpdActionDomain::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::icmpd
