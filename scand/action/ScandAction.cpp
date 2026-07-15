#include "action/ScandAction.h"

namespace pz::scand
{

ScandAction::ScandAction(ScandActionDomain domain) : m_domain(domain)
{
}

ScandActionDomain ScandAction::domain() const
{
    return m_domain;
}

}
