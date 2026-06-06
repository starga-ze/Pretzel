#pragma once

#include "action/IpcdAction.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

enum class IpcdBootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendServerHello  = 1,
    SendSyncResponse = 2
};

class IpcdBootstrapAction final : public IpcdAction
{
public:
    explicit IpcdBootstrapAction(IpcdBootstrapActionType type);

    IpcdBootstrapAction(IpcdBootstrapActionType type,
                        std::unique_ptr<nf::ipc::IpcMessage> request);

    IpcdBootstrapActionType type() const;
    const nf::ipc::IpcMessage* request() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeRequest();

    void dispatch(IpcdServiceManager& serviceManager) override;

private:
    IpcdBootstrapActionType m_type{IpcdBootstrapActionType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_request;
};

} // namespace nf::ipcd
