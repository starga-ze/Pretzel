#pragma once

#include "event/EventFactory.h"
#include "event/MgmtdEvent.h"
#include "grpc/GrpcProtocol.h"

#include "http/HttpMessage.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

class MgmtdEventFactory final : public pz::event::EventFactory<MgmtdEvent, MgmtdEventDomain>
{
public:
    MgmtdEventFactory() = default;
    ~MgmtdEventFactory() override = default;

    std::unique_ptr<MgmtdEvent> create() override;
    std::unique_ptr<MgmtdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<MgmtdEvent> create(MgmtdEventDomain domain, std::uint32_t type) override;

    // The two transports the base factory does not know about. mgmtd is the only daemon that
    // serves a browser and the only one that speaks to pretzel-ai, so these are its own overloads
    // rather than additions to the shared interface — but they are overloads of the same name for
    // the same reason the IPC one exists: deciding WHICH event a wire message becomes is this
    // class's job, and a router that decided it would be a router holding a routing table of its
    // own. Both return nullptr for a message no event maps to; postEvent drops a null.
    std::unique_ptr<MgmtdEvent> create(pz::http::HttpRequest request, pz::http::SessionId id);
    std::unique_ptr<MgmtdEvent> create(GrpcCmd cmd, std::uint32_t ticket, std::string json);
};

}
