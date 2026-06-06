#include "action/AuthdAction.h"

namespace nf::authd
{

AuthdAction::AuthdAction(AuthdActionDomain domain) : m_domain(domain)
{
}

AuthdActionDomain AuthdAction::domain() const
{
    return m_domain;
}

} // namespace nf::authd
