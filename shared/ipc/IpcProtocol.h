#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pz::ipc
{

// v2: bootstrap convergence payloads — RuntimeReady/SyncResponse carry applied_version,
// RuntimeStart carries target_version. All daemons are built from this header and
// deployed together, so the codec's exact-match version check guards against a stale
// binary that was not redeployed (the failure mode behind the snmpd zombie incident).
inline constexpr std::uint8_t IPC_PROTOCOL_VERSION = 2;
// 1 MiB ceiling: ScanResult payloads scale with the discovered fleet (one entry
// per interface/ARP/LLDP row), and a single PAN-OS appliance pulled in over the
// vendor API can alone contribute >64 KiB of ARP/interface data. The receive ring
// buffer (rx_buffer_size) must be >= this, since a frame is only dispatched once it
// fits whole in the buffer.
inline constexpr std::size_t IPC_MAX_FRAME_SIZE = 1024 * 1024;

enum class IpcDaemon : std::uint8_t
{
    Unknown   = 0,
    Ipcd      = 1,
    Engined   = 2,
    Authd     = 3,
    Icmpd     = 4,
    Scand     = 5,
    Topologyd = 6,
    Mgmtd     = 7,

    Broadcast = 255
};

enum class IpcCmd : std::uint16_t
{
    Unknown      = 0,
    
    ClientHello  = 1,
    ServerHello  = 2,
    
    // SyncRequest : engined -> ipcd, no meaningful payload.
    // SyncResponse: ipcd -> engined, payload = JSON
    //   {"daemons":[{"daemon":"icmpd","ready":bool,"generation":N,"applied_version":V}, ...]}.
    //   applied_version is the running-config version each daemon has loaded/applied
    //   (0 = not yet reported). engined gates readiness on applied_version >= target.
    SyncRequest  = 3,
    SyncResponse = 4,

    // RuntimeReady: service daemon -> ipcd, payload = JSON
    //   {"daemon":"icmpd","applied_version":V}. V is the config version the daemon has
    //   applied. A bare daemon-name string (legacy) is tolerated as applied_version=0.
    // RuntimeStart: engined -> broadcast, payload = JSON {"target_version":V} — the epoch
    //   the fleet has converged to. RuntimeStop: reserved.
    RuntimeReady = 5,
    RuntimeStart = 6,
    RuntimeStop  = 7,

    ApiRequest   = 100,
    ApiResponse  = 101,
    
    Error        = 102,

    // Unicast from icmpd to engined: payload = JSON {"alive":N,"ips":[...]}.
    // engined holds the alive-IP snapshot and feeds it to the SNMP scan.
    ProbeResult       = 103,
    HeartbeatRequest  = 104,
    HeartbeatResponse = 105,
    HeartbeatResult   = 106,

    // Unicast from mgmtd to engined after a config commit.
    ConfigReloadRequest  = 108,

    // Unicast from engined to mgmtd once RuntimeStart has been broadcast —
    // signals that all service daemons are back up and the reload is complete.
    ConfigReloadResponse = 109,

    // Unicast from engined to each service daemon: restart via execv.
    ConfigReload      = 107,

    // Unicast from mgmtd to engined: payload = changes JSON array.
    // Engined persists config, fans out ConfigReload, and replies with
    // ConfigReloadResponse when all service daemons are back up.
    SettingsCommitRequest = 110,

    // Unicast from engined to mgmtd: payload = commit-queue snapshot JSON.
    // Sent whenever the queue changes (task added, started, completed, failed).
    CommitQueueStatus = 111,

    // Unicast from engined to scand: payload = JSON {"ips":["..."]} to scan.
    // engined drives the scan on its own timer using the latest probe alive IPs.
    ScanRequest = 112,

    // Unicast from scand to engined: payload = JSON {"devices":[...]} results.
    // engined (the single DB writer) persists them into the probe_devices table (SNMP/API columns).
    ScanResult = 113,

    // Unicast from mgmtd to engined: payload = JSON {"username","password_hash","salt"}.
    // engined (the single DB writer) writes it into running_config (mgmtd.service.http
    // .admin) as a new version; mgmtd applies it in-memory optimistically. No response,
    // no ConfigReload (mgmtd is the sole consumer of the credential).
    AdminPasswordUpdate = 114,

    // Unicast from engined to icmpd: no payload. Triggers one ICMP probe cycle.
    // icmpd replies with ProbeResult once the cycle completes.
    ProbeRequest = 115,
};

enum class IpcFlag : std::uint8_t
{
    None      = 0x00,
    Request   = 0x01,
    Response  = 0x02,
    Error     = 0x04,
    Broadcast = 0x08,
    Retransmit = 0x10
};

#pragma pack(push, 1)
struct IpcWireHeader
{
    std::uint8_t  version;
    std::uint8_t  src;
    std::uint8_t  dst;
    std::uint8_t  flags;
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
    static std::string flagsToStr(std::uint8_t flags);

    static IpcDaemon strToDaemon(const std::string& daemon) noexcept;
};

} // namespace pz::ipc
