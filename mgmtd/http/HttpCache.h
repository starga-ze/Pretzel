#pragma once

#include <optional>
#include <string>

namespace pz::mgmtd
{

class HttpCache
{
public:
    struct Entry
    {
        std::string contentType;
        std::string body;
    };

    explicit HttpCache(std::string baseDir);

    std::optional<Entry> get(const std::string& target) const;

private:
    static std::string loadFile(const std::string& path);
    static std::string contentType(const std::string& target);
    static bool safeTarget(const std::string& target);

private:
    std::string m_baseDir;
};

} // namespace pz::mgmtd
