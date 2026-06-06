#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nf::ipc
{

inline constexpr std::uint8_t IPC_PROTOCOL_VERSION = 1;
inline constexpr std::size_t IPC_MAX_FRAME_SIZE = 64 * 1024;

enum class IpcDaemon : std::uint8_t
{
    Unknown   = 0,
    Ipcd      = 1,
    Engined   = 2,
    Authd     = 3,
    Icmpd     = 4,
    Snmpd     = 5,
    Topologyd = 6,
    Mgmtd     = 7,

    Broadcast = 255
};

enum class IpcCmd : std::uint16_t
{
    Unknown      = 0,
    
    ClientHello  = 1,
    ServerHello  = 2,
    
    SyncRequest  = 3,
    SyncResponse = 4,

    RuntimeReady = 5,
    RuntimeStart = 6,
    RuntimeStop  = 7,

    ApiRequest   = 100,
    ApiResponse  = 101,
    
    Error        = 102,

    ProbeResult       = 103,
    HeartbeatRequest  = 104,
    HeartbeatResponse = 105,
    HeartbeatResult   = 106,
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

} // namespace nf::ipc
