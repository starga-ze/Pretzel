#include "service/probe/ProbeService.h"

#include "service/EnginedServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::engined
{

void ProbeService::handleEvent(EnginedServiceManager& serviceManager,
                               const ProbeEvent& event)
{
    if (event.type() != ProbeEventType::ReceiveProbeResult)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in)
    {
        LOG_WARN("ProbeService: empty ProbeResult — dropping");
        return;
    }

    // Awareness: engined sees every probe result before it reaches mgmtd.
    LOG_INFO("ProbeService: relaying ProbeResult icmpd→mgmtd bytes={}",
             in->getPayload().size());

    // Relay unchanged to mgmtd, re-stamping the source as engined.
    auto out = std::make_unique<pz::ipc::IpcMessage>();
    out->setSrc(pz::ipc::IpcDaemon::Engined);
    out->setDst(pz::ipc::IpcDaemon::Mgmtd);
    out->setCmd(pz::ipc::IpcCmd::ProbeResult);
    out->setFlags(in->getFlags());
    out->setPayload(in->getPayload());

    serviceManager.txRouter().handleIpcMessage(std::move(out));
}

} // namespace pz::engined
