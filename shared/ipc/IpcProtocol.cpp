#include "ipc/IpcProtocol.h"

#include <arpa/inet.h>

namespace nf::ipc
{

std::uint8_t IpcProtocol::orFlag(IpcFlag lhs, IpcFlag rhs) noexcept
{
    return static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs);
}

bool IpcProtocol::hasFlag(std::uint8_t flags, IpcFlag flag) noexcept
{
    return (flags & static_cast<std::uint8_t>(flag)) != 0;
}

IpcWireHeader IpcProtocol::hostToNet(const IpcWireHeader& h) noexcept
{
    IpcWireHeader out = h;
    out.cmd = htons(out.cmd);
    out.reserved = htons(out.reserved);
    out.seqNo = htonl(out.seqNo);
    out.payloadLen = htonl(out.payloadLen);
    return out;
}

IpcWireHeader IpcProtocol::netToHost(const IpcWireHeader& h) noexcept
{
    IpcWireHeader out = h;
    out.cmd = ntohs(out.cmd);
    out.reserved = ntohs(out.reserved);
    out.seqNo = ntohl(out.seqNo);
    out.payloadLen = ntohl(out.payloadLen);
    return out;
}

const char* IpcProtocol::daemonToStr(IpcDaemon daemon) noexcept
{
    switch (daemon)
    {
    case IpcDaemon::Ipcd:      return "ipcd";
    case IpcDaemon::Engined:   return "engined";
    case IpcDaemon::Topologyd: return "topologyd";
    case IpcDaemon::Mgmtd:     return "mgmtd";
    case IpcDaemon::Broadcast: return "broadcast";
    default:                   return "unknown";
    }
}

const char* IpcProtocol::cmdToStr(IpcCmd cmd) noexcept
{
    switch (cmd)
    {
    case IpcCmd::ClientHello:  return "ClientHello";
    case IpcCmd::ServerHello:  return "ServerHello";
    case IpcCmd::Sync:         return "Sync";
    case IpcCmd::RuntimeRequest: return "RuntimeRequest";
    case IpcCmd::RuntimeResponse: return "RuntimeResponse";
    case IpcCmd::ApiRequest:   return "ApiRequest";
    case IpcCmd::ApiResponse:  return "ApiResponse";
    case IpcCmd::Error:        return "Error";
    default:                   return "Unknown";
    }
}

std::string IpcProtocol::flagsToStr(std::uint8_t flags)
{
    if (flags == static_cast<std::uint8_t>(IpcFlag::None))
        return "None";

    std::string out;

    auto append = [&](const char* s)
    {
        if (!out.empty())
            out += "|";
        out += s;
    };

    if (hasFlag(flags, IpcFlag::Request))
        append("Request");
    if (hasFlag(flags, IpcFlag::Response))
        append("Response");
    if (hasFlag(flags, IpcFlag::Error))
        append("Error");
    if (hasFlag(flags, IpcFlag::Broadcast))
        append("Broadcast");

    return out;
}

} // namespace nf::ipc
