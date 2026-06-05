#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nf::icmpd
{

enum class IcmpIoResult
{
    Ok,
    WouldBlock,
    PeerClosed,
    Error,
};

class IcmpConnection final
{
public:
    explicit IcmpConnection(int fd);

    IcmpConnection(const IcmpConnection&) = delete;
    IcmpConnection& operator=(const IcmpConnection&) = delete;

    IcmpConnection(IcmpConnection&&) = delete;
    IcmpConnection& operator=(IcmpConnection&&) = delete;

    IcmpIoResult recv(std::vector<std::uint8_t>& outBytes,
                      std::string& outSrcIp,
                      int& outErrno);

    IcmpIoResult send(const std::vector<std::uint8_t>& bytes,
                      const std::string& dstIp,
                      int& outErrno);

    int fd() const;

private:
    int m_fd {-1};
};

} // namespace nf::icmpd
