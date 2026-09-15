#include "algorithm/Timestamp.h"

namespace pz::algorithm
{

std::string utcTimestamp(std::chrono::system_clock::time_point at)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(at);

    std::tm tm{};
    // gmtime_r rather than gmtime: the daemons are single-threaded loops today, but a shared helper
    // that would break the day one of them is not is not worth the two characters.
    if (gmtime_r(&t, &tm) == nullptr)
        return {};

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0)
        return {};

    return buf;
}

std::string utcTimestamp()
{
    return utcTimestamp(std::chrono::system_clock::now());
}

}
