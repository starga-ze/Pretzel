// ISO-8601 UTC, the format three places were producing by hand.
//
// The trailing Z is the part that matters: it is what tells an IdP the value is UTC rather than
// local time, and a SAML clock-skew window is judged against it. A copy that drifted would not
// fail loudly — it would make a skew check compare two things measured differently.

#include "algorithm/Timestamp.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace pz::algorithm;
using namespace std::chrono;

TEST(UtcTimestamp, RendersAKnownInstantExactly)
{
    // 2026-09-15T04:05:06Z as seconds since the epoch.
    const auto at = system_clock::time_point(seconds(1789445106));
    EXPECT_EQ("2026-09-15T04:05:06Z", utcTimestamp(at));
}

TEST(UtcTimestamp, IsUtcRatherThanLocalTime)
{
    // The epoch itself: any local-time rendering outside UTC would show a different hour or date.
    EXPECT_EQ("1970-01-01T00:00:00Z", utcTimestamp(system_clock::time_point{}));
}

TEST(UtcTimestamp, HasTheShapeAnXmlDateTimeNeeds)
{
    const std::string now = utcTimestamp();

    ASSERT_EQ(20u, now.size()) << now;
    EXPECT_EQ('-', now[4]) << now;
    EXPECT_EQ('-', now[7]) << now;
    EXPECT_EQ('T', now[10]) << now;
    EXPECT_EQ(':', now[13]) << now;
    EXPECT_EQ(':', now[16]) << now;
    EXPECT_EQ('Z', now[19]) << now;
}

TEST(UtcTimestamp, TruncatesToWholeSecondsRatherThanRounding)
{
    const auto base = system_clock::time_point(seconds(1789445106));

    EXPECT_EQ("2026-09-15T04:05:06Z", utcTimestamp(base));
    EXPECT_EQ("2026-09-15T04:05:06Z", utcTimestamp(base + milliseconds(999)));
    EXPECT_EQ("2026-09-15T04:05:07Z", utcTimestamp(base + seconds(1)));
}

TEST(UtcTimestamp, MovesForwardWithTheClock)
{
    const auto at = system_clock::now();
    EXPECT_LT(utcTimestamp(at - hours(1)), utcTimestamp(at)) << "the format sorts lexicographically, by design";
    EXPECT_LT(utcTimestamp(at), utcTimestamp(at + hours(1)));
}
