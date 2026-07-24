#include "service/logtail/LogTailService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>

namespace pz::engined
{

namespace
{

using pz::db::Database;

const std::string kLogDir = "/var/log/pretzel";

// Every daemon writing under kLogDir. engined tails its own file too — its ingest lines just become
// data like any other, bounded by retention.
const std::vector<std::string> kDaemons = {"ipcd", "engined", "mgmtd", "authd", "icmpd", "scand", "topologyd"};

constexpr auto kPollInterval = std::chrono::seconds(2);
constexpr auto kPruneInterval = std::chrono::seconds(60);
constexpr int kRetentionDays = 7;

// Cap one pass so a huge burst (or a first-time large file) can't stall the main loop; the rest is
// caught up on subsequent passes.
constexpr std::uint64_t kMaxReadBytes = 4u * 1024u * 1024u;

// Rows per INSERT statement — keeps each statement and its parameter list a sane size.
constexpr std::size_t kInsertBatch = 200;

// A file log line: "[2026-07-22 13:46:19.996][INFO][File.cpp:89] message" once ANSI is stripped.
// Groups: 1=timestamp 2=level 3=source-location 4=message.
const std::regex& headerRe()
{
    static const std::regex re(R"(^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{1,6})\]\[([A-Za-z]+)\]\[([^\]]*)\]\s?(.*)$)");
    return re;
}

// spdlog colours the level in-band (short_level_flag), so file lines carry ESC sequences. Strip every
// CSI/escape run so the stored message is clean text — this is the bulk of the old "noise".
std::string stripAnsi(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();)
    {
        if (in[i] == '\033')
        {
            // Skip ESC then an optional '[' and the parameter/intermediate bytes up to the final byte.
            ++i;
            if (i < in.size() && in[i] == '[')
            {
                ++i;
                while (i < in.size() && !(in[i] >= '@' && in[i] <= '~'))
                    ++i;
                if (i < in.size())
                    ++i; // consume the final byte
            }
            else if (i < in.size())
            {
                ++i; // a lone two-byte escape
            }
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

int levelFromText(const std::string& lvl)
{
    if (lvl == "TRACE")
        return 0;
    if (lvl == "DEBUG")
        return 1;
    if (lvl == "INFO")
        return 2;
    if (lvl == "WARN")
        return 3;
    if (lvl == "ERROR")
        return 4;
    if (lvl == "CRITICAL")
        return 5;
    return 2; // "unkn" and anything unexpected map to info
}

}

void LogTailService::start()
{
    m_started = true;
    LOG_INFO("log tail service started (daemons={})", kDaemons.size());
}

void LogTailService::poll(std::chrono::steady_clock::time_point now)
{
    if (!m_started)
        return;

    if (now - m_lastPollAt < kPollInterval)
        return;
    m_lastPollAt = now;

    if (!Database::instance().isConnected())
        return;

    for (const auto& daemon : kDaemons)
        tailDaemon(daemon);

    if (now - m_lastPruneAt >= kPruneInterval)
    {
        m_lastPruneAt = now;
        prune();
    }
}

void LogTailService::tailDaemon(const std::string& daemon)
{
    auto& db = Database::instance();
    const std::string path = kLogDir + "/" + daemon + ".log";

    struct ::stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return; // no file yet — daemon not started

    const std::uint64_t inode = static_cast<std::uint64_t>(st.st_ino);
    const std::uint64_t size = static_cast<std::uint64_t>(st.st_size);

    std::uint64_t offset = 0;
    std::uint64_t ckInode = 0;
    bool haveCk = false;
    {
        const auto rows = db.queryRows("SELECT inode, offset_b FROM system_log_offset WHERE daemon=$1", {daemon});
        if (!rows.empty() && rows[0].size() >= 2)
        {
            ckInode = std::strtoull(rows[0][0].c_str(), nullptr, 10);
            offset = std::strtoull(rows[0][1].c_str(), nullptr, 10);
            haveCk = true;
        }
    }

    // First sight of this daemon: start at the current end so we tail forward instead of backfilling a
    // possibly-large existing file (old format, already-rotated history). Rows accrue from now on.
    if (!haveCk)
    {
        db.exec("INSERT INTO system_log_offset (daemon, inode, offset_b) VALUES ($1,$2,$3) "
                "ON CONFLICT (daemon) DO UPDATE SET inode=EXCLUDED.inode, offset_b=EXCLUDED.offset_b, updated_at=now()",
                {daemon, std::to_string(inode), std::to_string(size)});
        return;
    }

    // Rotation (fresh inode) or truncation (shrunk below our mark): restart at the head of the file.
    if (inode != ckInode || size < offset)
        offset = 0;

    if (offset >= size)
    {
        if (inode != ckInode)
            db.exec("UPDATE system_log_offset SET inode=$2, updated_at=now() WHERE daemon=$1",
                    {daemon, std::to_string(inode)});
        return;
    }

    std::uint64_t want = size - offset;
    if (want > kMaxReadBytes)
        want = kMaxReadBytes;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return;
    f.seekg(static_cast<std::streamoff>(offset));
    std::string buf(static_cast<std::size_t>(want), '\0');
    f.read(buf.data(), static_cast<std::streamsize>(want));
    buf.resize(static_cast<std::size_t>(f.gcount()));

    // Whole lines only: anything past the last newline is a partial line still being written — leave
    // it in the file and pick it up next pass.
    const auto lastNl = buf.find_last_of('\n');
    if (lastNl == std::string::npos)
        return;
    buf.resize(lastNl + 1);

    // Fold lines into entries. A header line opens an entry; lines without a header continue the one
    // before them (multi-line messages). Each entry remembers its byte position so the last, possibly
    // still-growing, entry can be left unconsumed for next pass.
    struct Pending
    {
        std::uint64_t pos;
        Entry entry;
    };
    std::vector<Pending> pending;

    std::size_t start = 0;
    for (std::size_t i = 0; i < buf.size(); ++i)
    {
        if (buf[i] != '\n')
            continue;

        std::string raw = buf.substr(start, i - start);
        const std::uint64_t linePos = start;
        start = i + 1;

        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        const std::string line = stripAnsi(raw);

        std::smatch m;
        if (std::regex_search(line, m, headerRe()))
        {
            Entry e;
            e.ts = m[1].str();
            e.level = levelFromText(m[2].str());
            e.loc = m[3].str();
            e.message = m[4].str();
            pending.push_back({linePos, std::move(e)});
        }
        else if (!pending.empty())
        {
            pending.back().entry.message += '\n';
            pending.back().entry.message += line;
        }
        // else: a continuation with no open entry (offset landed mid-message) — drop as noise.
    }

    std::vector<Entry> toInsert;
    std::uint64_t newOffset = offset;

    if (pending.size() >= 2)
    {
        // Every entry but the last was closed by a following header, so it is complete.
        toInsert.reserve(pending.size() - 1);
        for (std::size_t i = 0; i + 1 < pending.size(); ++i)
            toInsert.push_back(std::move(pending[i].entry));
        newOffset = offset + pending.back().pos; // leave the last entry's bytes for next pass
    }
    else if (pending.size() == 1)
    {
        // A lone entry might still be growing; don't insert it, but skip any leading noise before it.
        newOffset = offset + pending.front().pos;
        if (newOffset == offset)
            return; // nothing to consume yet — wait for the next entry to close this one
    }
    else
    {
        // No header at all in this region: orphan/noise lines. Consume them so we don't rescan.
        newOffset = offset + buf.size();
    }

    flushBatch(daemon, toInsert, inode, newOffset);
}

bool LogTailService::flushBatch(const std::string& daemon, const std::vector<Entry>& entries, std::uint64_t inode,
                                std::uint64_t newOffset)
{
    auto& db = Database::instance();

    // Rows and the checkpoint move together: on any failure we roll back and leave the offset where it
    // was, so the same bytes are retried next pass rather than lost or half-written.
    if (!db.exec("BEGIN"))
        return false;

    for (std::size_t i = 0; i < entries.size(); i += kInsertBatch)
    {
        const std::size_t n = std::min(kInsertBatch, entries.size() - i);
        std::string sql = "INSERT INTO system_log (ts, daemon, level, loc, message) VALUES ";
        std::vector<std::string> params;
        params.reserve(n * 5);
        for (std::size_t j = 0; j < n; ++j)
        {
            const std::size_t base = j * 5;
            if (j)
                sql += ',';
            sql += "($" + std::to_string(base + 1) + "::timestamptz,$" + std::to_string(base + 2) + ",$" +
                   std::to_string(base + 3) + "::smallint,NULLIF($" + std::to_string(base + 4) + ",''),$" +
                   std::to_string(base + 5) + ")";
            const auto& e = entries[i + j];
            params.push_back(e.ts);
            params.push_back(daemon);
            params.push_back(std::to_string(e.level));
            params.push_back(e.loc);
            params.push_back(e.message);
        }
        if (!db.exec(sql, params))
        {
            db.exec("ROLLBACK");
            return false;
        }
    }

    if (!db.exec("INSERT INTO system_log_offset (daemon, inode, offset_b) VALUES ($1,$2,$3) "
                 "ON CONFLICT (daemon) DO UPDATE SET inode=EXCLUDED.inode, offset_b=EXCLUDED.offset_b, updated_at=now()",
                 {daemon, std::to_string(inode), std::to_string(newOffset)}))
    {
        db.exec("ROLLBACK");
        return false;
    }

    if (!db.exec("COMMIT"))
    {
        db.exec("ROLLBACK");
        return false;
    }

    // Deliberately silent: logging each batch here would write to engined.log, which this very tailer
    // then re-ingests next pass — a self-feeding trickle that never goes quiet even when idle. The
    // inserted rows are their own evidence; start() already logs that the service is running.
    return true;
}

void LogTailService::prune()
{
    Database::instance().exec("DELETE FROM system_log WHERE ts < now() - ($1 || ' days')::interval",
                              {std::to_string(kRetentionDays)});
}

}
