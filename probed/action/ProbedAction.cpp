#include "action/ProbedAction.h"

namespace pz::probed
{

ProbedAction::ProbedAction(ProbedActionDomain domain) : m_domain(domain)
{
}

ProbedActionDomain ProbedAction::domain() const
{
    return m_domain;
}

}
