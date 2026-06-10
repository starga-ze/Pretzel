#pragma once

#include "event/SnmpdEvent.h"
#include "ipc/IpcMessage.h"
#include "snmp/SnmpTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace pz::snmpd
{

enum class ScanEventType : std::uint32_t
{
    Unknown                = 0,
    ReceiveSnmpScanRequest = 1,
    ScanComplete           = 2,
};

class ScanEvent final : public SnmpdEvent
{
public:
    explicit ScanEvent(ScanEventType type);

    // For ReceiveSnmpScanRequest — carries the raw IPC message.
    ScanEvent(ScanEventType type,
              std::unique_ptr<pz::ipc::IpcMessage> message);

    // For ScanComplete — carries the responding devices.
    ScanEvent(ScanEventType type,
              std::vector<SnmpDevice> devices);

    void dispatch(SnmpdServiceManager& serviceManager) override;

    ScanEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    const std::vector<SnmpDevice>& devices() const;

private:
    ScanEventType m_type{ScanEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
    std::vector<SnmpDevice> m_devices;
};

} // namespace pz::snmpd
