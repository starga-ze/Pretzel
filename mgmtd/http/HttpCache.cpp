#include "http/HttpCache.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pz::mgmtd
{

namespace
{

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

HttpCache::HttpCache(std::string baseDir)
    : m_baseDir(std::move(baseDir))
{
}

std::optional<HttpCache::Entry> HttpCache::get(const std::string& target) const
{
    if (!safeTarget(target))
    {
        return std::nullopt;
    }

    const std::string normalized = target == "/" ? "/index.html" : target;
    const std::string filePath = m_baseDir + normalized;

    try
    {
        return Entry{contentType(normalized), loadFile(filePath)};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string HttpCache::loadFile(const std::string& path)
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

std::string HttpCache::contentType(const std::string& target)
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

bool HttpCache::safeTarget(const std::string& target)
{
    return !target.empty() && target.front() == '/' && target.find("..") == std::string::npos;
}

} // namespace pz::mgmtd
