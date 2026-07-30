#pragma once

#include "event/CollectordEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

enum class ApiEventType : std::uint32_t
{
    Unknown = 0,
    // Inbound IPC — one per connector-test operation, so route() dispatches straight to the owning
    // controller (no in-payload mode branch).
    RunKeygenTest = 1,    // → CredentialController: issue/validate a credential
    RunEndpointTest = 2,  // → EndpointController: call an endpoint (keygen first if no key)
    RunSaseTest = 3,      // → StatusController: SASE device health + store api-key
    ReceiveKeyState = 4,  // issued-key cache update (repo, stays in ApiService)
    // Schedule-driven (injected by ApiService::schedule, not from the wire).
    Setup = 5,        // one-shot after bootstrap: fetch issued keys + arm periodic collection
    RunPeriodic = 6,  // recurring: SASE health probe + credential auto-refresh (each self-gated)
};

class ApiEvent final : public CollectordEvent
{
public:
    explicit ApiEvent(ApiEventType type);

    ApiEvent(ApiEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(CollectordServiceManager& serviceManager) override;

    ApiEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    ApiEventType m_type{ApiEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
