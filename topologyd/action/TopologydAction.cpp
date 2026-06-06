#include "action/TopologydAction.h"

namespace nf::topologyd
{

TopologydAction::TopologydAction(TopologydActionDomain domain)
    : m_domain(domain)
{
}

TopologydActionDomain TopologydAction::domain() const
{
    return m_domain;
}

} // namespace nf::topologyd
