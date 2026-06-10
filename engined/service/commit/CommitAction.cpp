#include "service/commit/CommitAction.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

CommitAction::CommitAction(CommitActionType type)
    : EnginedAction(EnginedActionDomain::Commit),
      m_type(type)
{
}

CommitActionType CommitAction::type() const
{
    return m_type;
}

void CommitAction::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.commitService().handleAction(serviceManager, *this);
}

} // namespace pz::engined
