#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace pz::engined
{

// Tails each daemon's spdlog file into the system_log table. Runs on the engined main loop (poll()
// is called from EnginedServiceManager::schedule), so it shares the single-threaded Database handle
// with every other writer — no locking needed. Per daemon it keeps a byte-offset + inode checkpoint
// in system_log_offset; each pass reads only the bytes appended since, strips ANSI, folds multi-line
// entries, and batch-inserts the parsed rows. The files stay the source of truth; this table is the
// queryable index the web UI reads. Rows + checkpoint advance together in one transaction, so a crash
// mid-pass re-reads from the last committed offset rather than losing or half-writing a batch.
class LogTailService
{
public:
    void start();

    // Rate-limited internally: does real work at most once per kPollInterval and prunes aged rows at
    // most once per kPruneInterval, so it is cheap to call on every tick.
    void poll(std::chrono::steady_clock::time_point now);

private:
    // One fully-formed log entry, ready to insert. A multi-line message (e.g. an IPC dump) arrives as
    // a header line followed by continuation lines with no header of their own; those are folded into
    // `message` here so the row carries the whole entry.
    struct Entry
    {
        std::string ts;      // "YYYY-MM-DD HH:MM:SS.mmm" — local wall clock, straight from the line
        std::string loc;     // "file.cpp:line"; empty when the line carried none
        std::string message; // ANSI-stripped; folded continuation lines joined with '\n'
        int level{2};        // 0=trace 1=debug 2=info 3=warn 4=error 5=critical
    };

    void tailDaemon(const std::string& daemon);
    bool flushBatch(const std::string& daemon, const std::vector<Entry>& entries, std::uint64_t inode,
                    std::uint64_t newOffset);
    void prune();

    std::chrono::steady_clock::time_point m_lastPollAt{};
    std::chrono::steady_clock::time_point m_lastPruneAt{};
    bool m_started{false};
};

}
