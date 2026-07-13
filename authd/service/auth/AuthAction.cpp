#include "service/auth/AuthAction.h"
#include "service/AuthdServiceManager.h"

namespace pz::authd
{

AuthAction::AuthAction(AuthActionType type,
                       pz::ipc::IpcDaemon dst,
                       std::uint32_t seqNo,
                       std::string payload) :
    AuthdAction(AuthdActionDomain::Auth),
    m_type(type),
    m_dst(dst),
    m_seqNo(seqNo),
    m_payload(std::move(payload))
{
}

AuthActionType AuthAction::type() const
{
    return m_type;
}

pz::ipc::IpcDaemon AuthAction::dst() const
{
    return m_dst;
}

std::uint32_t AuthAction::seqNo() const
{
    return m_seqNo;
}

const std::string& AuthAction::payload() const
{
    return m_payload;
}

void AuthAction::dispatch(AuthdServiceManager& serviceManager)
{
    serviceManager.authService().handleAction(serviceManager, *this);
}

} // namespace pz::authd
