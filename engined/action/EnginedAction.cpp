#include "action/EnginedAction.h"

namespace nf::engined
{

EnginedAction::EnginedAction(EnginedActionDomain domain) : m_domain(domain)
{
}

EnginedActionDomain EnginedAction::domain() const
{
    return m_domain;
}

} // namespace nf::engined
