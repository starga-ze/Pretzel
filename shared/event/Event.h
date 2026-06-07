#pragma once

#include <chrono>
#include <cstdint>

namespace pz::event
{

class Event
{
public:
    Event() = default;
    virtual ~Event() = default;

    std::chrono::steady_clock::time_point createdAt() const
    {
        return m_createdAt;
    }

private:
    std::chrono::steady_clock::time_point m_createdAt { std::chrono::steady_clock::now() };
};

} // namespace pz::event
