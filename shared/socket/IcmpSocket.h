#pragma once

namespace pz::socket
{

class IcmpSocket final
{
public:
    IcmpSocket() = default;
    ~IcmpSocket();

    IcmpSocket(const IcmpSocket&) = delete;
    IcmpSocket& operator=(const IcmpSocket&) = delete;

    IcmpSocket(IcmpSocket&&) = delete;
    IcmpSocket& operator=(IcmpSocket&&) = delete;

    bool open();
    void close();

    int fd() const;

private:
    bool createSocket();
    bool setNonBlocking(int fd);

private:
    int m_fd{-1};
};

}
