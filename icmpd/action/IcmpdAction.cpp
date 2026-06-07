#include "action/IcmpdAction.h"

namespace pz::icmpd
{

IcmpdAction::IcmpdAction(IcmpdActionDomain domain)
    : m_domain(domain)
{
}

IcmpdActionDomain IcmpdAction::domain() const
{
    return m_domain;
}


} // namespace pz::icmpd
