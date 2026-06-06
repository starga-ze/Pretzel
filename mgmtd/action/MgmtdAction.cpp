#include "action/MgmtdAction.h"

namespace nf::mgmtd
{

MgmtdAction::MgmtdAction(MgmtdActionDomain domain) : m_domain(domain)
{
}

MgmtdActionDomain MgmtdAction::domain() const
{
    return m_domain;
}

} // namespace nf::mgmtd
