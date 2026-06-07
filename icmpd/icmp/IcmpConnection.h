#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

namespace pz::icmpd
{

enum class IcmpIoResult
{
    Ok,
    WouldBlock,
    PeerClosed,
    BufferFull,
    Error,
};

class IcmpConnection final
{
public:
    struct RxFrame
    {
        std::vector<std::uint8_t> bytes;
        std::string srcIp;
    };

    struct TxFrame
    {
        std::vector<std::uint8_t> bytes;
        std::string dstIp;
    };

public:
    explicit IcmpConnection(int fd,
                            std::size_t rxQueueLimit = DEFAULT_RX_QUEUE_LIMIT,
                            std::size_t txQueueLimit = DEFAULT_TX_QUEUE_LIMIT);

    IcmpConnection(const IcmpConnection&) = delete;
    IcmpConnection& operator=(const IcmpConnection&) = delete;

    IcmpConnection(IcmpConnection&&) = delete;
    IcmpConnection& operator=(IcmpConnection&&) = delete;

    int fd() const noexcept;

    IcmpIoResult recv(int& outErrno);
    IcmpIoResult send(int& outErrno);

    bool write(std::vector<std::uint8_t> bytes,
               std::string dstIp);

    bool read(RxFrame& outFrame);

    bool hasPendingRx() const noexcept;
    bool hasPendingTx() const noexcept;

    std::size_t rxQueueSize() const noexcept;
    std::size_t txQueueSize() const noexcept;

private:
    static constexpr std::size_t DEFAULT_RX_QUEUE_LIMIT = 4096;
    static constexpr std::size_t DEFAULT_TX_QUEUE_LIMIT = 4096;
    static constexpr std::size_t RECV_BUFFER_SIZE = 65536;

private:
    int m_fd {-1};

    std::size_t m_rxQueueLimit {DEFAULT_RX_QUEUE_LIMIT};
    std::size_t m_txQueueLimit {DEFAULT_TX_QUEUE_LIMIT};

    std::queue<RxFrame> m_rxQueue;
    std::queue<TxFrame> m_txQueue;
};

} // namespace pz::icmpd
