#include "action/CollectordAction.h"

namespace pz::collectord
{

CollectordAction::CollectordAction(CollectordActionDomain domain) : m_domain(domain)
{
}

CollectordActionDomain CollectordAction::domain() const
{
    return m_domain;
}

}
