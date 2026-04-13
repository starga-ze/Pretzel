#pragma once

#include "ipc/IpcProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nf::ipc
{

class IpcMessage
{
public:
    IpcMessage();
    IpcMessage(IpcDaemon srcDaemon,
               IpcDaemon dstDaemon,
               IpcCmd command,
               std::uint8_t messageFlags,
               std::uint32_t sequenceNo,
               std::vector<std::uint8_t> body);

    bool isRequest() const noexcept;
    bool isResponse() const noexcept;
    bool isError() const noexcept;
    bool isBroadcast() const noexcept;

    std::string dump() const;

public:
    std::uint8_t version;
    IpcDaemon src;
    IpcDaemon dst;
    std::uint8_t flags;
    IpcCmd cmd;
    std::uint32_t seqNo;
    std::vector<std::uint8_t> payload;
};

} // namespace nf::ipc
