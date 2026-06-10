#pragma once

namespace pz::socket
{

class UdpSocket final
{
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&&) = delete;
    UdpSocket& operator=(UdpSocket&&) = delete;

    bool open();
    void close();

    int fd() const;

private:
    bool setNonBlocking(int fd);

    int m_fd{-1};
};

} // namespace pz::socket
