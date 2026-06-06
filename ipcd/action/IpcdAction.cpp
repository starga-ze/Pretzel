#include "action/IpcdAction.h"

namespace nf::ipcd
{

IpcdAction::IpcdAction(IpcdActionDomain domain) : m_domain(domain)
{
}

IpcdActionDomain IpcdAction::domain() const
{
    return m_domain;
}

} // namespace nf::ipcd
