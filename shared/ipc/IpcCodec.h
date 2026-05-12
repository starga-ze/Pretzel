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
    std::vector<std::uint8_t> encode(const IpcMessage& msg) const;
    std::vector<std::uint8_t> encode(const std::unique_ptr<IpcMessage>& msg) const;

    bool encode(const IpcMessage& msg, std::vector<std::uint8_t>& out) const;
    bool encode(const std::unique_ptr<IpcMessage>& msg, std::vector<std::uint8_t>& out) const;

    IpcDecodeResult decode(const std::uint8_t* data, std::size_t len, IpcMessage& out) const;
    IpcDecodeResult decode(const std::uint8_t* data, std::size_t len, std::unique_ptr<IpcMessage>& out) const;

    IpcDecodeResult decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const;
    IpcDecodeResult decode(const std::vector<std::uint8_t>& frame, std::unique_ptr<IpcMessage>& out) const;

    IpcPeekResult peekFrameSize(const std::uint8_t* data, std::size_t len, std::size_t& outFrameSize) const;
};

} // namespace nf::ipc
