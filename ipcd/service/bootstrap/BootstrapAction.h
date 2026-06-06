#pragma once

#include "action/IpcdAction.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendServerHello  = 1,
    SendSyncResponse = 2
};

class BootstrapAction final : public IpcdAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapAction(BootstrapActionType type,
                        std::unique_ptr<nf::ipc::IpcMessage> request);

    BootstrapActionType type() const;
    const nf::ipc::IpcMessage* request() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeRequest();

    void dispatch(IpcdServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_request;
};

} // namespace nf::ipcd
