#pragma once

#include "event/ScandEvent.h"
#include "ipc/IpcMessage.h"
#include "snmp/SnmpTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace pz::scand
{

enum class ScanEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveScanRequest = 1,
    SnmpScanComplete = 2,
    ApiScanComplete = 3,
};

class ScanEvent final : public ScandEvent
{
public:
    explicit ScanEvent(ScanEventType type);

    ScanEvent(ScanEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    ScanEvent(ScanEventType type, std::vector<SnmpDevice> devices);

    void dispatch(ScandServiceManager& serviceManager) override;

    ScanEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    const std::vector<SnmpDevice>& devices() const;

private:
    ScanEventType m_type{ScanEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
    std::vector<SnmpDevice> m_devices;
};

}
