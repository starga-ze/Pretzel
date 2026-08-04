#include "service/topology/TopologyEvent.h"

#include "service/TopologydServiceManager.h"

namespace pz::topologyd
{

TopologyEvent::TopologyEvent(TopologyEventType type)
    : TopologydEvent(TopologydEventDomain::Topology), m_type(type)
{
}

TopologyEvent::TopologyEvent(TopologyEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : TopologydEvent(TopologydEventDomain::Topology), m_type(type), m_message(std::move(message))
{
}

void TopologyEvent::dispatch(TopologydServiceManager& serviceManager)
{
    serviceManager.topologyService().handleEvent(serviceManager, *this);
}

TopologyEventType TopologyEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* TopologyEvent::message() const
{
    return m_message.get();
}

}
