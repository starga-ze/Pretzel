#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ScanEventType : std::uint32_t
{
    Unknown            = 0,
    ReceiveScanRequest = 1,  // mgmtd → engined → snmpd
    ReceiveScanResult  = 2,  // snmpd → engined → mgmtd
};

// Carries an SNMP scan request (mgmtd-originated) or result (snmpd-originated) that
// engined relays on to the other end. engined is on the path purely as the
// control-plane hub (relay + awareness); it does not own or interpret the payload.
class ScanEvent final : public EnginedEvent
{
public:
    explicit ScanEvent(ScanEventType type);
    ScanEvent(ScanEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    ScanEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    ScanEventType m_type{ScanEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::engined
