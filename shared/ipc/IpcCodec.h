#pragma once

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nf::ipc
{

enum class IpcDecodeResult
{
    Ok,
    NeedMoreData,
    InvalidFrame,
    TooLarge
};

class IpcCodec
{
public:
    std::vector<std::uint8_t> encode(const IpcMessage& msg) const;
    IpcDecodeResult decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const;
    std::size_t peekFrameSize(const std::uint8_t* data, std::size_t len) const;
};

} // namespace nf::ipc
