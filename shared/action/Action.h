#pragma once

#include <chrono>
#include <cstdint>

namespace nf::action
{

class Action
{
public:
    Action() = default;
    virtual ~Action() = default;

    std::chrono::steady_clock::time_point createdAt() const
    {
        return m_createdAt;
    }

private:
    std::chrono::steady_clock::time_point m_createdAt{std::chrono::steady_clock::now()};
};

} // namespace nf::action
