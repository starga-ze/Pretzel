#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace pz::algorithm
{

// A result the browser will come back for, filed under the ticket it was handed.
//
// mgmtd's async endpoints all work the same way: the request is delegated over IPC or gRPC, a
// ticket is returned immediately, and the browser polls until the answer is filed. That gave
// MgmtdServiceManager six of these maps — SSO results, API test results, chat results, chat
// contexts, chat partials, retrieval results — each with its own hand-written put/take pair, its
// own copy of the bound, and its own copy of the comment explaining the bound.
//
// Two rules, and both are consequences of the browser being an unreliable participant:
//
//   · A result is taken ONCE. Polling is a drain, not a read, so a second poll gets nothing and a
//     stale answer cannot be served to a later turn that happens to reuse the ticket.
//
//   · The map is bounded, and clears WHOLESALE when it fills. A browser that navigated away never
//     drains its ticket, so entries accumulate with nothing to evict them. Aging them would need a
//     timestamp per entry to serve a case that costs one lost turn; clearing is what the map has
//     always done and the comment that said so is now here instead of in six places.
template <typename T>
class TicketMap
{
public:
    static constexpr std::size_t kDefaultBound = 256;

    explicit TicketMap(std::size_t bound = kDefaultBound) : m_bound(bound == 0 ? kDefaultBound : bound)
    {
    }

    // Tickets start at 1 so that 0 stays available as "no ticket" — the headless paths (a
    // credential auto-refresh, a scheduled poll) answer on seqNo 0 with nobody waiting.
    std::uint32_t next()
    {
        return m_next++;
    }

    void put(std::uint32_t ticket, T value)
    {
        if (m_entries.size() > m_bound)
            m_entries.clear();

        m_entries[ticket] = std::move(value);
    }

    std::optional<T> take(std::uint32_t ticket)
    {
        const auto it = m_entries.find(ticket);
        if (it == m_entries.end())
            return std::nullopt;

        T out = std::move(it->second);
        m_entries.erase(it);
        return out;
    }

    // For state that is discarded alongside a result rather than polled for on its own — the
    // partial accumulated for a turn that has now finished.
    void discard(std::uint32_t ticket)
    {
        m_entries.erase(ticket);
    }

    bool contains(std::uint32_t ticket) const
    {
        return m_entries.find(ticket) != m_entries.end();
    }

    // Read without draining, for the poll that shows progress while the turn is still running.
    const T* peek(std::uint32_t ticket) const
    {
        const auto it = m_entries.find(ticket);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    T& operator[](std::uint32_t ticket)
    {
        if (m_entries.size() > m_bound)
            m_entries.clear();

        return m_entries[ticket];
    }

    std::size_t size() const
    {
        return m_entries.size();
    }

    std::size_t bound() const
    {
        return m_bound;
    }

private:
    std::unordered_map<std::uint32_t, T> m_entries;
    std::uint32_t m_next{1};
    std::size_t m_bound;
};

}
