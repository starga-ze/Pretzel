#include "action/ApidAction.h"

namespace pz::apid
{

ApidAction::ApidAction(ApidActionDomain domain) : m_domain(domain)
{
}

ApidActionDomain ApidAction::domain() const
{
    return m_domain;
}

}
