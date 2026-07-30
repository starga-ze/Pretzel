#include "service/api/ApiAction.h"

#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

ApiAction::ApiAction(ApiActionType type) : CollectordAction(CollectordActionDomain::Api), m_type(type)
{
}

ApiActionType ApiAction::type() const
{
    return m_type;
}

void ApiAction::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.apiService().handleAction(serviceManager, *this);
}

}
