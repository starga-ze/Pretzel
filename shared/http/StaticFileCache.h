#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace pz::http
{

// Serves a read-only static web tree from a base directory. At construction it preloads
// every file under baseDir into memory (URL path -> body + content-type + ETag), so the
// single poll thread never does disk I/O per request; each entry's ETag (a content hash)
// lets the transport answer conditional GETs with 304 Not Modified (see HttpBeast.h).
//
// reload=true skips the preload and reads from disk on every request instead — for
// iterating on the frontend without restarting the daemon. The daemon owns that policy
// (e.g. reads an env var and passes the flag in); this class stays daemon-neutral.
class StaticFileCache
{
public:
    struct Entry
    {
        std::string contentType;
        std::string body;
        std::string etag;   // quoted content hash, e.g. "\"9f2b1c...\""
    };

    explicit StaticFileCache(std::string baseDir, bool reload = false);

    std::optional<Entry> get(const std::string& target) const;

private:
    static std::string loadFile(const std::string& path);
    static std::string contentType(const std::string& target);
    static std::string makeEtag(const std::string& body);
    static bool        safeTarget(const std::string& target);
    static std::string normalize(const std::string& target);  // "/" -> "/index.html"

    void preload();

    std::string m_baseDir;
    bool        m_reload{false};                       // disk-read-per-request (dev)
    std::unordered_map<std::string, Entry> m_files;    // preloaded assets (normal mode)
};

} // namespace pz::http
