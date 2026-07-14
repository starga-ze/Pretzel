#include "http/StaticFileCache.h"

#include "util/Logger.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pz::http
{

namespace
{

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

StaticFileCache::StaticFileCache(std::string baseDir, bool reload)
    : m_baseDir(std::move(baseDir)),
      m_reload(reload)
{
    if (m_reload)
    {
        LOG_INFO("static assets: disk-reload mode (base={})", m_baseDir);
        return;
    }

    preload();
}

void StaticFileCache::preload()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(m_baseDir, ec))
    {
        LOG_WARN("static asset dir missing — will 404 (base={})", m_baseDir);
        return;
    }

    std::size_t files = 0, bytes = 0;
    for (auto it = fs::recursive_directory_iterator(m_baseDir, ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;

        // Key = "/" + path relative to base, forward slashes (URL form).
        const std::string rel = fs::relative(it->path(), m_baseDir, ec).generic_string();
        if (ec || rel.empty())
            continue;
        const std::string key = "/" + rel;

        std::string body;
        try
        {
            body = loadFile(it->path().string());
        }
        catch (...)
        {
            LOG_WARN("static preload skipped (path={})", it->path().string());
            continue;
        }

        Entry e;
        e.contentType = contentType(key);
        e.etag        = makeEtag(body);
        bytes += body.size();
        e.body        = std::move(body);

        m_files.emplace(key, std::move(e));
        ++files;
    }

    LOG_INFO("static assets preloaded (files={}, bytes={}, base={})", files, bytes, m_baseDir);
}

std::optional<StaticFileCache::Entry> StaticFileCache::get(const std::string& target) const
{
    if (!safeTarget(target))
        return std::nullopt;

    const std::string key = normalize(target);

    // Dev: read from disk on every request so frontend edits show without a restart.
    if (m_reload)
    {
        try
        {
            std::string body = loadFile(m_baseDir + key);
            Entry e;
            e.contentType = contentType(key);
            e.etag        = makeEtag(body);
            e.body        = std::move(body);
            return e;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    const auto it = m_files.find(key);
    if (it == m_files.end())
        return std::nullopt;
    return it->second;
}

std::string StaticFileCache::loadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("open failed: " + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string StaticFileCache::contentType(const std::string& target)
{
    if (endsWith(target, ".html")) return "text/html; charset=utf-8";
    if (endsWith(target, ".js")) return "application/javascript; charset=utf-8";
    if (endsWith(target, ".css")) return "text/css; charset=utf-8";
    if (endsWith(target, ".json")) return "application/json; charset=utf-8";
    if (endsWith(target, ".svg")) return "image/svg+xml";
    if (endsWith(target, ".png")) return "image/png";
    if (endsWith(target, ".ico")) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

// FNV-1a 64-bit over the body, mixed with length, rendered as a quoted hex strong ETag.
// Not a security hash — just a cheap content validator for conditional GETs.
std::string StaticFileCache::makeEtag(const std::string& body)
{
    std::uint64_t h = 1469598103934665603ULL;  // FNV offset basis
    for (unsigned char c : body)
    {
        h ^= c;
        h *= 1099511628211ULL;                  // FNV prime
    }
    h ^= body.size();

    char buf[24];
    std::snprintf(buf, sizeof(buf), "\"%016llx\"", static_cast<unsigned long long>(h));
    return buf;
}

bool StaticFileCache::safeTarget(const std::string& target)
{
    return !target.empty() && target.front() == '/' && target.find("..") == std::string::npos;
}

std::string StaticFileCache::normalize(const std::string& target)
{
    return target == "/" ? "/index.html" : target;
}

} // namespace pz::http
