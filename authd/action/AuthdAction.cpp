#include "action/AuthdAction.h"

namespace pz::authd
{

AuthdAction::AuthdAction(AuthdActionDomain domain) : m_domain(domain)
{
}

AuthdActionDomain AuthdAction::domain() const
{
    return m_domain;
}

}
