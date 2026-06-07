#include "action/SnmpdAction.h"

namespace pz::snmpd
{

SnmpdAction::SnmpdAction(SnmpdActionDomain domain) : m_domain(domain)
{
}

SnmpdActionDomain SnmpdAction::domain() const
{
    return m_domain;
}

} // namespace pz::snmpd
