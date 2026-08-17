#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pz::ipc
{

inline constexpr std::uint8_t IPC_PROTOCOL_VERSION = 2;
inline constexpr std::size_t IPC_MAX_FRAME_SIZE = 1024 * 1024;

enum class IpcDaemon : std::uint8_t
{
    Unknown = 0,
    Ipcd = 1,
    Engined = 2,
    Authd = 3,
    Probed = 4,
    Collectord = 5,
    Topologyd = 6,
    Mgmtd = 7,
    Apid = 8,
    Inferd = 9,
    // Retired. Retrieval was briefly its own daemon; it lives in inferd now, because embedding the
    // query and answering from it are the same concern and splitting them bought a serialisation
    // hop and a second failure mode. The value is kept rather than reused: ids are a wire contract
    // mirrored in inferd/transport/ipc_protocol.py, and handing 10 to a future daemon would make
    // an old peer's frames route to it silently.
    Ragd = 10,

    Broadcast = 255
};

enum class IpcCmd : std::uint16_t
{
    Unknown = 0,

    // ── Lifecycle (transport): handshake, config sync, runtime orchestration, transport error ──
    ClientHello = 1,
    ServerHello = 2,
    SyncRequest = 3,
    SyncResponse = 4,
    RuntimeReady = 5,
    RuntimeStart = 6,
    Error = 102,

    // ── Heartbeat (engined ↔ every daemon liveness) ──
    HeartbeatRequest = 104,
    HeartbeatResponse = 105,
    HeartbeatResult = 106,

    // ── Config distribution (engined authors and fans out) ──
    ConfigReloadRequest = 108,
    ConfigReloadResponse = 109,
    // Was ConfigReload. One-way broadcast that tells every daemon to apply the newly committed
    // config; deliberately named apart from the ConfigReload{Request,Response} RPC above.
    ConfigApply = 107,

    // ── Settings & admin (mgmtd → engined; engined is the sole DB writer) ──
    SettingsCommitRequest = 110,
    // Was CommitQueueStatus. engined → mgmtd, commit-queue progress for the settings-commit flow.
    SettingsCommitStatus = 111,
    AdminPasswordUpdate = 114,

    // ── Probe (ICMP reachability; probed, a privileged raw-socket daemon) ──
    ProbeRequest = 115,   // → probed: run a reachability sweep
    ProbeResult = 103,    // probed → engined: alive set, persisted to device status

    // ── Scan (collectord) ──
    ScanRequest = 112,    // → collectord
    ScanResult = 113,     // → engined

    // ── Auth (mgmtd → authd, delegated request/response) ──
    AuthLoginRequest = 116,
    AuthLoginResponse = 117,
    AuthOidcStartRequest = 118,
    AuthOidcStartResponse = 119,
    AuthOidcCallbackRequest = 120,
    AuthOidcCallbackResponse = 121,
    AuthSamlStartRequest = 122,
    AuthSamlStartResponse = 123,
    AuthSamlAcsRequest = 124,
    AuthSamlAcsResponse = 125,

    // ── Api (vendor credential lifecycle + collection + SASE device health key). These names/edges
    //    are reworked in the probed→collectord API migration (per-op split: keygen / endpoint-test /
    //    egress), so treat the group below as pending until that lands. ──
    //
    // engined is the only database writer, so the outcome of a key generation (already-encrypted
    // secret, expiry, test result) is handed over rather than written twice.
    ApiCredentialStateUpdate = 126,
    // Connector tests run in collectord, not mgmtd: collectord is the daemon that will poll these
    // devices on a schedule, and a test that exercised a different code path than the collector
    // would not be testing much. mgmtd forwards the operator's (possibly uncommitted) target and
    // correlates the reply by seqNo — the same shape as the SAML ACS delegation to authd. One cmd
    // per operation so collectord routes straight to the owning controller (no in-payload mode).
    ApiKeygenRequest = 127,        // mgmtd → collectord: issue/validate a credential (ngfw key / sase token)
    ApiConnectorTestResponse = 128,// collectord → mgmtd: result of any of the three tests (by seqNo)
    ApiEndpointTestRequest = 134,  // mgmtd → collectord: call an endpoint with a key (keygen first if none)
    ApiSaseTestRequest = 135,      // mgmtd → collectord: SASE device health (getPrismaAccessIP) + store api-key
    ApiTlsProbeRequest = 136,      // mgmtd → collectord: TLS-only handshake to a device, returns its cert
                                   // fingerprint so an NGFW can be pinned at creation (no credentials)
    ApiSaseKeyStoreRequest = 137,  // mgmtd → collectord: seal a SASE device's health api-key and hand it
                                   // to engined. Storing is its own operation, not a side effect of a
                                   // passing test, so the key survives a save that was never tested.
    ApiCredentialStoreRequest = 138,  // mgmtd → collectord: seal an API Key's account credential
                                      // (username/password) and hand it to engined. Same reason as
                                      // above — the operator's password must not live in one browser.
    // collectord asks engined for the issued keys. engined answers with the SEALED blobs and
    // collectord opens them with credentials.key — the plaintext never crosses the socket. collectord
    // caches the result rather than asking per call, because periodic collection would otherwise hit
    // the database on every poll.
    ApiCredentialStateRequest = 129,
    ApiCredentialStateResponse = 130,
    // collectord → engined: one connector's scheduled endpoint poll result, persisted to api_collection.
    ApiCollectionSample = 131,
    // probed → engined: a validated SASE device health api-key, sealed, to store in sase_device.api_key_enc.
    SaseApiKeyUpdate = 132,
    // collectord → engined: SASE control-plane health outcome (alive/down targets + egress-IP payloads),
    // persisted to sase_device.status/egress_result. The SASE counterpart of ProbeResult, which stays
    // ICMP/NGFW-only now that the SASE probe runs in collectord rather than riding probed's message.
    SaseHealthResult = 133,

    // ── Topology (mgmtd ↔ topologyd) ──
    // mgmtd owns no topology logic: it asks topologyd for one site's composed picture and serves
    // whatever it last received. topologyd reads the collected samples, correlates them and answers.
    // A request/response pair rather than a Write, because the composition is derived and lives in
    // memory — it is cheap to rebuild and never worth a table.
    TopologyRequest = 139,    // mgmtd → topologyd: compose this site (payload {site})
    TopologyResponse = 140,   // topologyd → mgmtd: the composed model

    // ── Inference (mgmtd ↔ inferd) ──
    // A chat turn on its way to an upstream model, through the AI gateway. It lives out here rather
    // than in mgmtd for the same reason every other outbound call does: the exchange takes seconds,
    // and mgmtd answers the console on the loop those seconds would be spent on.
    //
    // Correlated by seqNo, which is also the ticket the browser polls on — mgmtd never holds the
    // HTTP response open (see ApiConnectorTestResponse for the same shape). That is deliberate
    // beyond the first pass: streaming will deliver partial turns on this same edge without the
    // request/response contract changing.
    ChatRequest = 141,        // mgmtd → inferd: one turn (payload {model, message, ...})
    ChatResponse = 142,       // inferd → mgmtd: the completed turn, or why it did not complete

    // Corpus retrieval, the step before a turn reaches a model. Split from ChatRequest because the
    // operator is meant to see the passages and judge them before anything goes upstream — an
    // answer is only as good as what was retrieved, and a miss is worth catching there.
    RetrieveRequest = 143,    // mgmtd → inferd: {query, k, docset?, version?}
    RetrieveResponse = 144,   // inferd → mgmtd: {hits, took_ms, model, k}, or why there are none
};

// Coarse role of a command, orthogonal to its domain. Feeds IpcProtocol::isRoutingAllowed, which
// carries the one cross-cutting routing invariant this motivated — a `Write` only ever legitimately
// targets engined, the sole DB writer.
enum class CmdCategory : std::uint8_t
{
    Lifecycle,   // transport, heartbeat, runtime — infra, not a feature edge
    Config,      // config reload/apply distribution
    Auth,        // mgmtd → authd delegated request/response
    DeviceOp,    // ask a worker to act on something outside the appliance — a managed device
                 // (probe / scan / api test) or an upstream service (a chat turn through the gateway)
    Write,       // mutate engined's store — dst must be Engined
    Read,        // query engined's store
};

enum class IpcFlag : std::uint8_t
{
    None = 0x00,
    Request = 0x01,
    Response = 0x02,
    Error = 0x04,
    Broadcast = 0x08
};

#pragma pack(push, 1)
struct IpcWireHeader
{
    std::uint8_t version;
    std::uint8_t src;
    std::uint8_t dst;
    std::uint8_t flags;
    std::uint16_t cmd;
    std::uint16_t reserved;
    std::uint32_t seqNo;
    std::uint32_t payloadLen;
};
#pragma pack(pop)

static_assert(sizeof(IpcWireHeader) == 16, "IpcWireHeader must be 16 bytes");

class IpcProtocol
{
public:
    static std::uint8_t toFlag(IpcFlag flag) noexcept;
    static std::uint8_t orFlag(IpcFlag lhs, IpcFlag rhs) noexcept;
    static bool hasFlag(std::uint8_t flags, IpcFlag flag) noexcept;

    static IpcWireHeader hostToNet(const IpcWireHeader& h) noexcept;
    static IpcWireHeader netToHost(const IpcWireHeader& h) noexcept;

    static const char* daemonToStr(IpcDaemon daemon) noexcept;
    static const char* cmdToStr(IpcCmd cmd) noexcept;
    static CmdCategory classify(IpcCmd cmd) noexcept;

    // Routing invariant for the IPC fabric, derived from a command's CmdCategory: "route by data
    // ownership, not by hierarchy". Whatever the exact (src, dst) edge, a state mutation may only be
    // addressed to engined — the sole DB writer — so a Write pointed anywhere else is a misroute.
    //
    // Not yet a full (src, dst, cmd) allowlist: enumerating every legitimate edge (the auth
    // delegations in particular) is a later pass, so unconstrained categories return true and this
    // can be wired into Ipcd in warn-only mode without dropping valid traffic.
    static bool isRoutingAllowed(IpcDaemon src, IpcDaemon dst, IpcCmd cmd) noexcept;

    static std::string flagsToStr(std::uint8_t flags);

    static IpcDaemon strToDaemon(const std::string& daemon) noexcept;
};

}
