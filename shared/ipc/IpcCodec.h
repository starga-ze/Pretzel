#pragma once

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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

enum class IpcPeekResult
{
    Ok,
    NeedMoreData,
    InvalidFrame
};

class IpcCodec
{
public:
    std::vector<std::uint8_t> encode(const std::unique_ptr<IpcMessage>& msg) const;
    IpcDecodeResult decode(IpcFrameView frame, std::unique_ptr<IpcMessage>& out) const;

    IpcPeekResult peekFrameSize(const std::uint8_t* data, std::size_t len, std::size_t& outFrameSize) const;
};

} // namespace nf::ipc
