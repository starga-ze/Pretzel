#pragma once

#include "event/ApidEvent.h"
#include "event/EventFactory.h"

#include "http/HttpMessage.h"

#include <memory>

namespace pz::apid
{

class ApidEventFactory final : public pz::event::EventFactory<ApidEvent, ApidEventDomain>
{
public:
    ApidEventFactory() = default;
    ~ApidEventFactory() override = default;

    std::unique_ptr<ApidEvent> create() override;
    std::unique_ptr<ApidEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<ApidEvent> create(ApidEventDomain domain, std::uint32_t type) override;

    // The transport the base factory does not know about. apid is one of the two daemons that
    // serve HTTP, so this is its own overload — but an overload of the same name for the same
    // reason the IPC one exists: deciding WHICH event a wire message becomes is this class's job,
    // and a router that decided it would be a router holding a routing table of its own.
    std::unique_ptr<ApidEvent> create(pz::http::HttpRequest request, pz::http::SessionId id);
};

}
