#include "action/EnginedAction.h"

namespace pz::engined
{

EnginedAction::EnginedAction(EnginedActionDomain domain) : m_domain(domain)
{
}

EnginedActionDomain EnginedAction::domain() const
{
    return m_domain;
}

}
