#pragma once

#include "event/MgmtdEvent.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

// Everything that reaches the web domain, as one event.
//
// Three transports feed it and they are not interchangeable — a browser request is work to do and
// answer, while an IPC or gRPC message is an answer to work this daemon already asked for — but all
// three end in the same place: the controller that owns the route concerned. One event type is what
// lets that be one entry point (WebService::handleEvent) reached by one dispatch(), so adding a
// transport later does not mean adding a third way in.

// Which transport this event came off, and therefore which of the three payloads below is the live
// one. Set by the constructor that filled that payload, so it cannot disagree with its contents.
enum class WebEventType : std::uint32_t
{
    Unknown = 0,
    Http = 1,   // a browser asked for something   → routeHttpEvent
    Ipc = 2,    // another daemon answered         → routeIpcEvent
    Grpc = 3,   // pretzel-ai answered             → routeGrpcEvent
};

// What a daemon's answer is an answer TO. Decided in MgmtdEventFactory, where every inbound IPC
// message is already being read for its command: the wire says ApiConnectorTestResponse, the
// factory says which half of mgmtd was waiting for it, and WebService then only has to look at
// this. Naming it here rather than letting the controllers re-read IpcCmd keeps the set of answers
// the web domain accepts in one list — a command nobody claims never becomes a WebEvent at all.
//
// Why an event at all, when the router could simply call the store: because that is the pattern
// every other daemon follows, and because a router that writes state is a router that quietly
// acquires domain logic. The TopologyResponse branch had already grown a JSON parse and a
// drop-on-malformed rule inside MgmtdRxRouter; the SettingsCommitResponse branch discarded engined's
// error flag because there was no handler in which checking it was the obvious thing to do.
enum class WebIpcEventType : std::uint32_t
{
    Unknown = 0,
    TopologyResponse = 1,         // topologyd composed a site      → TopologyController
    SettingsCommitStatus = 2,     // engined's commit queue moved   → SettingsController
    ApiConnectorTestResponse = 3, // collectord ran a device call   → ApiController
    // 4, 5 (ChatResponse, RetrieveResponse) retired: chat moved to the pretzel-ai gRPC transport,
    // whose answers arrive inline via GrpcClientHandler, not as inbound IPC events.
    // 6 (GatewayCredentialStoreResponse) retired: sealing a vendor key no longer takes a round
    // trip. It used to be delegated to a daemon that has since been deleted; mgmtd now seals in
    // AiController and writes straight to engined, so there is no response to file. The number
    // stays spent rather than reused.
    AiModelUpdateResponse = 7,    // collectord fetched a vendor's
                                  //   model list                   → AiController
};

// The same for pretzel-ai, and decided in the same place. This is where the vendor's many calls
// collapse into the few shapes an answer can have: every benchtest read answers once and is
// collected once, so the ten of them share one value rather than carrying ten names that route
// identically.
enum class WebGrpcEventType : std::uint32_t
{
    Unknown = 0,
    ChatResponse = 1,          // a turn came back            → ChatController
    CorpusStatusResponse = 3,  // store snapshot read         → TechDocController
    CorpusRefreshProgress = 4, // one progress message        → TechDocController
    CorpusDocumentList = 5,    // one book's documents        → TechDocController
    BenchtestResponse = 6,     // any benchtest answer        → BenchtestController
    // A run reports many times and is read by any number of polls, so it overwrites one live slot
    // rather than resolving a ticket — the same shape a corpus refresh has.
    BenchtestRunProgress = 7,  // one run message             → BenchtestController
    ModelListResponse = 8,     // the picker's catalog        → ChatController
    CorpusExceptionList = 9,   // what a run did not collect  → TechDocController
};

class WebEvent final : public MgmtdEvent
{
public:
    // One constructor per transport, each stamping its own WebEventType: the tag is not a separate
    // argument because a caller able to pass it would be a caller able to pass the wrong one.
    //
    // HTTP is the only one that owes a reply, which is why it alone carries the session — the
    // response is written by WebAction against this same id.
    WebEvent(pz::http::HttpRequest request, pz::http::SessionId sessionId);
    WebEvent(WebIpcEventType ipcType, std::unique_ptr<pz::ipc::IpcMessage> message);
    WebEvent(WebGrpcEventType grpcType, std::uint32_t ticket, std::string json);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    WebEventType type() const;

    // Http.
    const pz::http::HttpRequest& request() const;
    pz::http::SessionId sessionId() const;

    // Ipc.
    WebIpcEventType ipcType() const;
    const pz::ipc::IpcMessage* message() const;

    // Grpc.
    WebGrpcEventType grpcType() const;
    std::uint32_t ticket() const;
    const std::string& json() const;

    // The body, moved out. The controller is this answer's only reader and most of them store it
    // whole, so it is handed over rather than copied — a benchtest export is measured in megabytes.
    // The event is spent afterwards, which is why this is non-const and why nothing reads it twice.
    std::string takeJson();

private:
    WebEventType m_type{WebEventType::Unknown};

    pz::http::HttpRequest m_request;
    pz::http::SessionId m_sessionId{0};

    WebIpcEventType m_ipcType{WebIpcEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;

    WebGrpcEventType m_grpcType{WebGrpcEventType::Unknown};
    std::uint32_t m_ticket{0};
    std::string m_json;
};

}
