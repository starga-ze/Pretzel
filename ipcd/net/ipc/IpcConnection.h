#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nf::ipcd
{

class IpcConnection
{
public:
    explicit IpcConnection(int fd, std::size_t rxBufferSize, std::size_t txBufferSize);
    ~IpcConnection();

    IpcConnection(const IpcConnection&) = delete;
    IpcConnection& operator=(const IpcConnection&) = delete;

    int fd() const noexcept;
    void close();

    std::uint8_t* rxWritePtr() noexcept;
    std::size_t rxWritable() const noexcept;
    void rxProduce(std::size_t n) noexcept;
    std::size_t rxReadable() const noexcept;
    void rxConsumeAll() noexcept;

    const std::uint8_t* txReadPtr() const noexcept;
    std::size_t txReadable() const noexcept;
    void txConsume(std::size_t n) noexcept;
    bool enqueueTx(const std::uint8_t* data, std::size_t len);

private:
    int m_fd {-1};

    std::vector<std::uint8_t> m_rxBuffer;
    std::size_t m_rxSize {0};

    std::vector<std::uint8_t> m_txBuffer;
    std::size_t m_txSize {0};
};

} // namespace nf::ipcd
