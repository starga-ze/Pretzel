#pragma once

#include <cstdint>

namespace nf::session
{

class Session
{
public:
    explicit Session(int fd);
    ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Session(Session&&) = default;
    Session& operator=(Session&&) = default;

public:
    int getFd() const noexcept;
    std::uint64_t getId() const noexcept;

private:
    static std::uint64_t nextId() noexcept;

private:
    int m_fd {-1};
    std::uint64_t m_id {0};
};

} // namespace nf::session
