#include "service/bootstrap/BootstrapAction.h"
#include "service/IpcdServiceManager.h"

namespace pz::ipcd
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : IpcdAction(IpcdActionDomain::Bootstrap), m_type(type)
{
}

BootstrapAction::BootstrapAction(BootstrapActionType type, std::unique_ptr<pz::ipc::IpcMessage> request)
    : IpcdAction(IpcdActionDomain::Bootstrap), m_type(type), m_request(std::move(request))
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* BootstrapAction::request() const
{
    return m_request.get();
}

std::unique_ptr<pz::ipc::IpcMessage> BootstrapAction::takeRequest()
{
    return std::move(m_request);
}

void BootstrapAction::dispatch(IpcdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
