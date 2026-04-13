#pragma once

#include <string>

namespace nf::ipcd
{

class UnixDomainSocket
{
public:
    explicit UnixDomainSocket(std::string socketPath);
    ~UnixDomainSocket();

    bool open();
    void close();
    int accept();
    
    int fd();
    const std::string& socketPath();

private:
    bool createSocket();
    bool setNonBlocking(int fd);
    bool bindSocket();
    bool listenSocket();

private:
    std::string m_socketPath;
    int m_fd {-1};
};

} // namespace nf::ipcd
