#pragma once

namespace nf::ipcd
{

class IpcConnection
{
public:
    explicit IpcConnection(int fd);
    ~IpcConnection();

    IpcConnection(const IpcConnection&) = delete;
    IpcConnection& operator=(const IpcConnection&) = delete;

    int fd() const noexcept;
    void close();

private:
    int m_fd {-1};
};

} // namespace nf::ipcd
