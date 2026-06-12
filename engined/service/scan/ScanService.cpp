#include "service/scan/ScanService.h"

#include "service/EnginedServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::engined
{

void ScanService::handleEvent(EnginedServiceManager& serviceManager,
                              const ScanEvent& event)
{
    const pz::ipc::IpcMessage* in = event.message();
    if (!in)
    {
        LOG_WARN("ScanService: empty scan message — dropping");
        return;
    }

    pz::ipc::IpcDaemon dst    = pz::ipc::IpcDaemon::Unknown;
    pz::ipc::IpcCmd    cmd    = pz::ipc::IpcCmd::Unknown;
    const char*        label  = "";

    switch (event.type())
    {
    case ScanEventType::ReceiveScanRequest:
        dst   = pz::ipc::IpcDaemon::Snmpd;
        cmd   = pz::ipc::IpcCmd::SnmpScanRequest;
        label = "ScanRequest mgmtd→snmpd";
        break;

    case ScanEventType::ReceiveScanResult:
        dst   = pz::ipc::IpcDaemon::Mgmtd;
        cmd   = pz::ipc::IpcCmd::SnmpResult;
        label = "SnmpResult snmpd→mgmtd";
        break;

    default:
        LOG_WARN("ScanService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        return;
    }

    // Awareness: engined sees every scan request and result in flight.
    LOG_INFO("ScanService: relaying {} bytes={}", label, in->getPayload().size());

    auto out = std::make_unique<pz::ipc::IpcMessage>();
    out->setSrc(pz::ipc::IpcDaemon::Engined);
    out->setDst(dst);
    out->setCmd(cmd);
    out->setFlags(in->getFlags());
    out->setPayload(in->getPayload());

    serviceManager.txRouter().handleIpcMessage(std::move(out));
}

} // namespace pz::engined
