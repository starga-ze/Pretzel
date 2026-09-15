// The result store behind mgmtd's async endpoints, written once instead of six times.
//
// The two rules it enforces are both consequences of the browser being an unreliable participant:
// a result is drained rather than read, so a stale answer can never be served to a later turn that
// reuses a ticket; and the map clears wholesale when it fills, because a browser that navigated
// away never comes back for its ticket and nothing else would ever evict it.

#include "algorithm/TicketMap.h"

#include <gtest/gtest.h>

#include <string>

using pz::algorithm::TicketMap;

// ── Tickets ─────────────────────────────────────────────────────────────────────────────────────

TEST(TicketMap, TicketsStartAtOneSoZeroStaysAvailableAsNoTicket)
{
    // The headless paths — a credential auto-refresh, a scheduled poll — answer on seqNo 0 with
    // nobody waiting. A ticket of 0 would be indistinguishable from one of those.
    TicketMap<std::string> map;
    EXPECT_EQ(1u, map.next());
}

TEST(TicketMap, TicketsAreMonotonicAndNeverRepeat)
{
    TicketMap<std::string> map;

    std::uint32_t previous = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const std::uint32_t ticket = map.next();
        EXPECT_GT(ticket, previous);
        previous = ticket;
    }
}

TEST(TicketMap, EachMapCountsIndependently)
{
    // The six stores in MgmtdServiceManager hand out their own ticket sequences; sharing one
    // counter would make a chat ticket collide with an API test ticket in neither's favour.
    TicketMap<std::string> a;
    TicketMap<std::string> b;

    a.next();
    a.next();

    EXPECT_EQ(1u, b.next());
}

// ── Take-once ───────────────────────────────────────────────────────────────────────────────────

TEST(TicketMap, AResultIsReturnedOnceAndThenGone)
{
    TicketMap<std::string> map;
    const auto ticket = map.next();

    map.put(ticket, R"({"status":"done"})");

    const auto first = map.take(ticket);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(R"({"status":"done"})", *first);

    EXPECT_FALSE(map.take(ticket).has_value()) << "polling is a drain, not a read";
}

TEST(TicketMap, AnUnknownTicketYieldsNothing)
{
    TicketMap<std::string> map;
    EXPECT_FALSE(map.take(999).has_value());
    EXPECT_FALSE(map.take(0).has_value());
}

TEST(TicketMap, PuttingTwiceUnderOneTicketKeepsTheLastValue)
{
    TicketMap<std::string> map;
    map.put(7, "first");
    map.put(7, "second");

    EXPECT_EQ("second", *map.take(7));
}

TEST(TicketMap, TicketsDoNotInterfereWithEachOther)
{
    TicketMap<std::string> map;
    map.put(1, "one");
    map.put(2, "two");

    EXPECT_EQ("one", *map.take(1));
    EXPECT_EQ("two", *map.take(2)) << "draining one ticket must not disturb another";
}

// ── peek, discard, contains ─────────────────────────────────────────────────────────────────────

TEST(TicketMap, PeekReadsWithoutDraining)
{
    // The progress poll: the turn is still running, so the partial has to survive being looked at.
    TicketMap<std::string> map;
    map.put(1, "partial answer so far");

    ASSERT_NE(nullptr, map.peek(1));
    EXPECT_EQ("partial answer so far", *map.peek(1));
    EXPECT_EQ(1u, map.size());

    EXPECT_EQ(nullptr, map.peek(2)) << "an unknown ticket has nothing to look at";
}

TEST(TicketMap, DiscardDropsAnEntryNobodyWillPollFor)
{
    // When a turn's result is drained, whatever was accumulating for it is dead weight.
    TicketMap<std::string> map;
    map.put(1, "accumulated");

    map.discard(1);
    EXPECT_FALSE(map.contains(1));
    EXPECT_EQ(0u, map.size());

    map.discard(999);   // absent is not an error
}

TEST(TicketMap, SubscriptAccumulatesInPlace)
{
    // The streaming case: each delta is appended to whatever the turn has produced so far.
    TicketMap<std::string> map;

    map[1] += "Hello";
    map[1] += ", world";

    EXPECT_EQ("Hello, world", *map.peek(1));
}

// ── The bound ───────────────────────────────────────────────────────────────────────────────────

TEST(TicketMap, ClearsWholesaleOnceItPassesItsBound)
{
    TicketMap<std::string> map(4);

    for (std::uint32_t i = 1; i <= 5; ++i)
        map.put(i, "x");
    EXPECT_EQ(5u, map.size()) << "the bound is a ceiling to exceed, not to stop at";

    // The next put finds the map over its bound and empties it before storing.
    map.put(6, "fresh");
    EXPECT_EQ(1u, map.size());
    EXPECT_EQ("fresh", *map.peek(6)) << "the value that triggered the clear must survive it";
}

TEST(TicketMap, TheSubscriptPathIsBoundedToo)
{
    // The partials map grows through operator[], not put; leaving that path unbounded would give a
    // stream of abandoned turns nothing to evict them.
    TicketMap<std::string> map(4);

    for (std::uint32_t i = 1; i <= 5; ++i)
        map[i] = "x";

    map[6] = "fresh";
    EXPECT_EQ(1u, map.size());
    EXPECT_EQ("fresh", *map.peek(6));
}

TEST(TicketMap, ADefaultMapBoundsAtTwoHundredAndFiftySix)
{
    TicketMap<std::string> map;
    EXPECT_EQ(256u, map.bound());

    for (std::uint32_t i = 1; i <= 257; ++i)
        map.put(i, "x");
    EXPECT_EQ(257u, map.size());

    map.put(258, "fresh");
    EXPECT_EQ(1u, map.size());
}

TEST(TicketMap, AZeroBoundFallsBackToTheDefaultRatherThanClearingEveryTime)
{
    // A bound of zero would clear on every put, making the map useless in a way that would only
    // show up as results that vanish before the browser polls.
    TicketMap<std::string> map(0);
    EXPECT_EQ(256u, map.bound());

    map.put(1, "kept");
    map.put(2, "also kept");
    EXPECT_EQ(2u, map.size());
}

// ── Non-string payloads ─────────────────────────────────────────────────────────────────────────

namespace
{

struct ChatContextLike
{
    std::string thread;
    int turn{0};
};

}

TEST(TicketMap, HoldsAMoveOnlyStyleStructAsWellAsAString)
{
    // m_chatContexts holds a struct, not a string; the template must not have been written for one
    // payload type by accident.
    TicketMap<ChatContextLike> map;
    map.put(1, ChatContextLike{"thread-a", 3});

    const auto taken = map.take(1);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ("thread-a", taken->thread);
    EXPECT_EQ(3, taken->turn);
    EXPECT_FALSE(map.contains(1));
}
