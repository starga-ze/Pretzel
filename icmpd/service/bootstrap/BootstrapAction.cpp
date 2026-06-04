#include "service/bootstrap/BootstrapAction.h"

namespace nf::icmpd
{

BootstrapAction::BootstrapAction(BootstrapActionType type)
    : IcmpdAction(IcmpdActionDomain::Bootstrap),
      m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

} // namespace nf::icmpd
