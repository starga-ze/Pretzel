#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ScanEventType : std::uint32_t
{
    Unknown           = 0,
    ReceiveScanResult = 1,  // scand → engined: persist devices to probe_devices
    TriggerScan       = 2,  // timer: ask scand to scan the latest probe alive IPs
};

// Carries an SNMP scan result (scand-originated) that engined — the single DB
// writer — persists into the probe_devices table. mgmtd reads that table for
// /api/devices.
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
