#include "ipc/IpcMessage.h"

#include <iomanip>
#include <sstream>

namespace nf::ipc
{

IpcMessage::IpcMessage() : 
    version(IPC_PROTOCOL_VERSION), 
    src(IpcDaemon::Unknown), 
    dst(IpcDaemon::Unknown),
    flags(static_cast<std::uint8_t>(IpcFlag::None)), 
    cmd(IpcCmd::Unknown), 
    seqNo(0)
{
}

IpcMessage::IpcMessage(IpcDaemon srcDaemon, IpcDaemon dstDaemon, IpcCmd command, std::uint8_t messageFlags,
                       std::uint32_t sequenceNo, std::vector<std::uint8_t> body) : 
    version(IPC_PROTOCOL_VERSION), 
    src(srcDaemon), 
    dst(dstDaemon), 
    flags(messageFlags), 
    cmd(command),
    seqNo(sequenceNo), 
    payload(std::move(body))
{
}

bool IpcMessage::isRequest() const noexcept
{
    return IpcProtocol::hasFlag(flags, IpcFlag::Request);
}

bool IpcMessage::isResponse() const noexcept
{
    return IpcProtocol::hasFlag(flags, IpcFlag::Response);
}

bool IpcMessage::isError() const noexcept
{
    return IpcProtocol::hasFlag(flags, IpcFlag::Error);
}

bool IpcMessage::isBroadcast() const noexcept
{
    return IpcProtocol::hasFlag(flags, IpcFlag::Broadcast);
}

std::string IpcMessage::dump() const
{
    std::ostringstream oss;

    oss << "[IPC MESSAGE]\n";
    oss << "  Version   : " << static_cast<int>(version) << "\n";
    oss << "  Src       : " << IpcProtocol::daemonToStr(src) << " (" << static_cast<int>(src) << ")\n";
    oss << "  Dst       : " << IpcProtocol::daemonToStr(dst) << " (" << static_cast<int>(dst) << ")\n";
    oss << "  Cmd       : " << IpcProtocol::cmdToStr(cmd) << " (" << static_cast<std::uint16_t>(cmd) << ")\n";
    oss << "  Flags     : " << IpcProtocol::flagsToStr(flags) << " (0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(flags) << std::dec << ")\n";
    oss << "  SeqNo     : " << seqNo << "\n";
    oss << "  Payload   : " << payload.size() << " bytes\n";

    if (!payload.empty())
    {
        oss << std::hex << std::setfill('0');

        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            if (i % 16 == 0)
                oss << "    ";

            oss << std::setw(2) << static_cast<int>(payload[i]) << " ";

            if (i % 16 == 15 || i + 1 == payload.size())
                oss << "\n";
        }

        oss << std::dec;
    }

    return oss.str();
}

} // namespace nf::ipc
