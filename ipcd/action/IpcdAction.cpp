#include "action/IpcdAction.h"

namespace pz::ipcd
{

IpcdAction::IpcdAction(IpcdActionDomain domain) : m_domain(domain)
{
}

IpcdActionDomain IpcdAction::domain() const
{
    return m_domain;
}

} // namespace pz::ipcd
