#include "action/MgmtdAction.h"

namespace pz::mgmtd
{

MgmtdAction::MgmtdAction(MgmtdActionDomain domain) : m_domain(domain)
{
}

MgmtdActionDomain MgmtdAction::domain() const
{
    return m_domain;
}

} // namespace pz::mgmtd
