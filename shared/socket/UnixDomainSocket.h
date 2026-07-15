#pragma once

#include <string>

namespace pz::socket
{

class UnixDomainSocket
{
public:
    enum class ConnectResult
    {
        Connected,
        InProgress,
        Failed
    };

public:
    explicit UnixDomainSocket(std::string socketPath);
    ~UnixDomainSocket();

    UnixDomainSocket(const UnixDomainSocket&) = delete;
    UnixDomainSocket& operator=(const UnixDomainSocket&) = delete;

    UnixDomainSocket(UnixDomainSocket&&) = delete;
    UnixDomainSocket& operator=(UnixDomainSocket&&) = delete;

    bool open();
    int accept();
    ConnectResult connect();

    void close();

    int fd() const;
    const std::string& socketPath() const;

private:
    bool createSocket();
    bool bindSocket();
    bool listenSocket();
    bool setNonBlocking(int fd);

private:
    static constexpr int LISTEN_BACKLOG = 64;

private:
    std::string m_socketPath;
    int m_fd{-1};
    bool m_shouldUnlinkOnClose{false};
};

}
