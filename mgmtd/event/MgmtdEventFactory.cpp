#include "event/MgmtdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/web/WebEvent.h"
#include "service/auth/AuthEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"

#include "util/Logger.h"

namespace pz::mgmtd
{

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(MgmtdEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case MgmtdEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case MgmtdEventDomain::Heartbeat:
        return std::make_unique<HeartbeatEvent>(static_cast<HeartbeatEventType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_DEBUG("received empty message — skipping");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case pz::ipc::IpcCmd::ServerHello:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveServerHello, std::move(msg));

    case pz::ipc::IpcCmd::RuntimeStart:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveRuntimeStart, std::move(msg));

    case pz::ipc::IpcCmd::HeartbeatRequest:
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::ReceiveHeartbeatRequest, std::move(msg));

    case pz::ipc::IpcCmd::HeartbeatResult:
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::ReceiveHeartbeatResult, std::move(msg));

    case pz::ipc::IpcCmd::SettingsCommitResponse:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveSettingsCommitResponse, std::move(msg));

    case pz::ipc::IpcCmd::AuthSamlAcsResponse:
        return std::make_unique<AuthEvent>(AuthEventType::ReceiveSamlAcsResponse, std::move(msg));

    // Answers a web-domain controller is waiting on. Each carries on to the controller that owns the
    // route serving it, so the ask and the answer stay in one file.
    case pz::ipc::IpcCmd::TopologyResponse:
        return std::make_unique<WebEvent>(WebIpcEventType::TopologyResponse, std::move(msg));

    case pz::ipc::IpcCmd::SettingsCommitStatus:
        return std::make_unique<WebEvent>(WebIpcEventType::SettingsCommitStatus, std::move(msg));

    case pz::ipc::IpcCmd::ApiConnectorTestResponse:
        return std::make_unique<WebEvent>(WebIpcEventType::ApiConnectorTestResponse, std::move(msg));

    case pz::ipc::IpcCmd::AiModelUpdateResponse:
        return std::make_unique<WebEvent>(WebIpcEventType::AiModelUpdateResponse, std::move(msg));

    default:
        LOG_WARN("unhandled cmd (cmd={})", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(pz::http::HttpRequest request, pz::http::SessionId id)
{
    return std::make_unique<WebEvent>(std::move(request), id);
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(GrpcCmd cmd, std::uint32_t ticket, std::string json)
{
    // The gRPC half of the switch above, and it reads the same way: one wire command, one event.
    // Several calls land on one value because their answers are the same shape — every benchtest
    // read answers once and is collected once — and that is a statement about the answers, which
    // belongs here beside the other transport's mapping rather than in a helper of its own.
    switch (cmd)
    {
    case GrpcCmd::Chat:
        return std::make_unique<WebEvent>(WebGrpcEventType::ChatResponse, ticket, std::move(json));

    case GrpcCmd::ListModels:
        return std::make_unique<WebEvent>(WebGrpcEventType::ModelListResponse, ticket, std::move(json));

    case GrpcCmd::CorpusStatus:
        return std::make_unique<WebEvent>(WebGrpcEventType::CorpusStatusResponse, ticket, std::move(json));

    case GrpcCmd::CorpusDocuments:
        return std::make_unique<WebEvent>(WebGrpcEventType::CorpusDocumentList, ticket, std::move(json));

    case GrpcCmd::CorpusExceptions:
        return std::make_unique<WebEvent>(WebGrpcEventType::CorpusExceptionList, ticket, std::move(json));

    case GrpcCmd::CorpusRefresh:
        return std::make_unique<WebEvent>(WebGrpcEventType::CorpusRefreshProgress, ticket, std::move(json));

    case GrpcCmd::BenchtestRun:
        return std::make_unique<WebEvent>(WebGrpcEventType::BenchtestRunProgress, ticket, std::move(json));

    case GrpcCmd::BenchtestDatasets:
    case GrpcCmd::BenchtestUpload:
    case GrpcCmd::BenchtestDelete:
    case GrpcCmd::BenchtestSummary:
    case GrpcCmd::BenchtestRows:
    case GrpcCmd::BenchtestExport:
    case GrpcCmd::BenchtestRunList:
    case GrpcCmd::BenchtestRunInfo:
    case GrpcCmd::BenchtestCases:
    case GrpcCmd::BenchtestCase:
        return std::make_unique<WebEvent>(WebGrpcEventType::BenchtestResponse, ticket, std::move(json));

    // Calls that answer to nobody. ApplyConfig is an acknowledgement — mgmtd states the
    // configuration, no browser is holding a screen open on the reply, and a failure is already in
    // GrpcClientHandler's log. A cancel is the same: it ends a stream rather than producing one.
    // Filing either would be looking for a consumer that should not exist.
    case GrpcCmd::ApplyConfig:
    case GrpcCmd::BenchtestCancel:
    case GrpcCmd::CorpusCancel:
        return nullptr;

    default:
        LOG_WARN("no event maps to rpc {} (ticket={})", grpcCmdToStr(cmd), ticket);
        return nullptr;
    }
}
}
