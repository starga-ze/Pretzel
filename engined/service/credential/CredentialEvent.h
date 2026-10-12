#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class CredentialEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveStateUpdate = 1,
    ReceiveStateRequest = 2,
    ReceiveSaseApiKey = 3,
    // The assistant's vendor API key, sealed by mgmtd. Same concern as the three above — a
    // secret that arrives already encrypted and lands in a table rather than running_config — so
    // it rides this service instead of earning one of its own.
    ReceiveAiCredential = 4,
    // collectord asking for ONE vendor's sealed key, for the model-catalog refresh. Distinct from
    // ReceiveStateRequest, which answers with every issued device key: the two read different
    // tables, and an answer carrying both would hand each caller the other's key material.
    ReceiveAiCredentialRequest = 5,
    // collectord handing back what a vendor listed, to replace that vendor's rows in
    // ai_provider_model. A write, like the three above, and for the same reason: collectord makes
    // the outbound call and engined owns the table.
    ReceiveAiModels = 6,
};

class CredentialEvent final : public EnginedEvent
{
public:
    explicit CredentialEvent(CredentialEventType type);
    CredentialEvent(CredentialEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    CredentialEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    CredentialEventType m_type{CredentialEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
