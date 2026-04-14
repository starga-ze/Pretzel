#pragma once

#include "ipc/IpcProtocol.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nf::ipc
{

struct IpcHeader
{
    std::uint8_t version {IPC_PROTOCOL_VERSION};
    IpcDaemon src {IpcDaemon::Unknown};
    IpcDaemon dst {IpcDaemon::Unknown};
    std::uint8_t flags {0};
    IpcCmd cmd {IpcCmd::Unknown};
    std::uint32_t seqNo {0};

    static IpcHeader build(IpcDaemon src,
                           IpcDaemon dst,
                           IpcCmd cmd,
                           std::uint32_t seqNo,
                           std::uint8_t flags = static_cast<std::uint8_t>(IpcFlag::Request))
    {
        IpcHeader header;
        header.version = IPC_PROTOCOL_VERSION;
        header.src = src;
        header.dst = dst;
        header.cmd = cmd;
        header.seqNo = seqNo;
        header.flags = flags;
        return header;
    }

    bool isRequest() const
    {
        return IpcProtocol::hasFlag(flags, IpcFlag::Request);
    }

    bool isResponse() const
    {
        return IpcProtocol::hasFlag(flags, IpcFlag::Response);
    }

    bool isError() const
    {
        return IpcProtocol::hasFlag(flags, IpcFlag::Error);
    }

    bool isBroadcast() const
    {
        return IpcProtocol::hasFlag(flags, IpcFlag::Broadcast);
    }
};

class IpcMessage
{
public:
    IpcMessage();

    IpcMessage(const IpcHeader& header,
               std::vector<std::uint8_t> payload = {});

    IpcMessage(IpcHeader&& header,
               std::vector<std::uint8_t> payload = {});

public:
    std::uint8_t getVersion() const;
    void setVersion(std::uint8_t version);

    IpcDaemon getSrc() const;
    void setSrc(IpcDaemon src);

    IpcDaemon getDst() const;
    void setDst(IpcDaemon dst);

    std::uint8_t getFlags() const;
    void setFlags(std::uint8_t flags);

    IpcCmd getCmd() const;
    void setCmd(IpcCmd cmd);

    std::uint32_t getSeqNo() const;
    void setSeqNo(std::uint32_t seqNo);

    const IpcHeader& getHeader() const;
    IpcHeader& getHeader();

    const std::vector<std::uint8_t>& getPayload() const;
    std::vector<std::uint8_t>& getPayload();

    void setPayload(const std::vector<std::uint8_t>& payload);
    void setPayload(std::vector<std::uint8_t>&& payload);
    void setPayload(const void* data, std::size_t len);

    std::size_t getPayloadLen() const;
    bool empty() const;

    bool isRequest() const;
    bool isResponse() const;
    bool isError() const;
    bool isBroadcast() const;

    IpcWireHeader toWireHeader() const;

    std::string dump() const;

private:
    IpcHeader m_header;
    std::vector<std::uint8_t> m_payload;
};

} // namespace nf::ipc
