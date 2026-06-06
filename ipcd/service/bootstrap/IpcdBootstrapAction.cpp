#include "service/bootstrap/IpcdBootstrapAction.h"
#include "service/IpcdServiceManager.h"

namespace nf::ipcd
{

IpcdBootstrapAction::IpcdBootstrapAction(IpcdBootstrapActionType type)
    : IpcdAction(IpcdActionDomain::Bootstrap),
      m_type(type)
{
}

IpcdBootstrapAction::IpcdBootstrapAction(IpcdBootstrapActionType type,
                                         std::unique_ptr<nf::ipc::IpcMessage> request)
    : IpcdAction(IpcdActionDomain::Bootstrap),
      m_type(type),
      m_request(std::move(request))
{
}

IpcdBootstrapActionType IpcdBootstrapAction::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* IpcdBootstrapAction::request() const
{
    return m_request.get();
}

std::unique_ptr<nf::ipc::IpcMessage> IpcdBootstrapAction::takeRequest()
{
    return std::move(m_request);
}

void IpcdBootstrapAction::dispatch(IpcdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace nf::ipcd
