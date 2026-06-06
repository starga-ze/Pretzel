#include "action/SnmpdAction.h"

namespace nf::snmpd
{

SnmpdAction::SnmpdAction(SnmpdActionDomain domain) : m_domain(domain)
{
}

SnmpdActionDomain SnmpdAction::domain() const
{
    return m_domain;
}

} // namespace nf::snmpd
