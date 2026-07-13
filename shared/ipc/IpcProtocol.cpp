#include "ipc/IpcProtocol.h"

#include <arpa/inet.h>

namespace pz::ipc
{

std::uint8_t IpcProtocol::toFlag(IpcFlag flag) noexcept
{
    return static_cast<std::uint8_t>(flag);
}

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
    case IpcDaemon::Authd:     return "authd";
    case IpcDaemon::Icmpd:     return "icmpd";
    case IpcDaemon::Scand:     return "scand";
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
    case IpcCmd::SyncRequest:  return "SyncRequest";
    case IpcCmd::SyncResponse: return "SyncResponse";
    case IpcCmd::RuntimeReady: return "RuntimeReady";
    case IpcCmd::RuntimeStart: return "RuntimeStart";
    case IpcCmd::Error:             return "Error";
    case IpcCmd::ProbeResult:       return "ProbeResult";
    case IpcCmd::HeartbeatRequest:  return "HeartbeatRequest";
    case IpcCmd::HeartbeatResponse: return "HeartbeatResponse";
    case IpcCmd::HeartbeatResult:   return "HeartbeatResult";
    case IpcCmd::ConfigReload:         return "ConfigReload";
    case IpcCmd::ConfigReloadRequest:   return "ConfigReloadRequest";
    case IpcCmd::ConfigReloadResponse:  return "ConfigReloadResponse";
    case IpcCmd::SettingsCommitRequest: return "SettingsCommitRequest";
    case IpcCmd::CommitQueueStatus:     return "CommitQueueStatus";
    case IpcCmd::ScanRequest:       return "ScanRequest";
    case IpcCmd::ScanResult:            return "ScanResult";
    case IpcCmd::AdminPasswordUpdate:   return "AdminPasswordUpdate";
    case IpcCmd::ProbeRequest:          return "ProbeRequest";
    case IpcCmd::AuthLoginRequest:          return "AuthLoginRequest";
    case IpcCmd::AuthLoginResponse:         return "AuthLoginResponse";
    case IpcCmd::AuthOidcStartRequest:      return "AuthOidcStartRequest";
    case IpcCmd::AuthOidcStartResponse:     return "AuthOidcStartResponse";
    case IpcCmd::AuthOidcCallbackRequest:   return "AuthOidcCallbackRequest";
    case IpcCmd::AuthOidcCallbackResponse:  return "AuthOidcCallbackResponse";
    case IpcCmd::AuthSamlStartRequest:      return "AuthSamlStartRequest";
    case IpcCmd::AuthSamlStartResponse:     return "AuthSamlStartResponse";
    case IpcCmd::AuthSamlAcsRequest:        return "AuthSamlAcsRequest";
    case IpcCmd::AuthSamlAcsResponse:       return "AuthSamlAcsResponse";
    default:                            return "Unknown";
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
    {
        append("Request");
    }
    if (hasFlag(flags, IpcFlag::Response))
    {
        append("Response");
    }
    if (hasFlag(flags, IpcFlag::Error))
    {
        append("Error");
    }
    if (hasFlag(flags, IpcFlag::Broadcast))
    {
        append("Broadcast");
    }

    return out;
}

pz::ipc::IpcDaemon IpcProtocol::strToDaemon(const std::string& daemon) noexcept
{
    if (daemon == "ipcd")
    {
        return IpcDaemon::Ipcd;
    }

    if (daemon == "engined")
    {
        return IpcDaemon::Engined;
    }

    if (daemon == "authd")
    {
        return IpcDaemon::Authd;
    }

    if (daemon == "icmpd")
    {
        return IpcDaemon::Icmpd;
    }

    if (daemon == "scand")
    {
        return IpcDaemon::Scand;
    }

    if (daemon == "topologyd")
    {
        return IpcDaemon::Topologyd;
    }

    if (daemon == "mgmtd")
    {
        return IpcDaemon::Mgmtd;
    }

    if (daemon == "broadcast")
    {
        return IpcDaemon::Broadcast;
    }

    return IpcDaemon::Unknown;
}

} // namespace pz::ipc
