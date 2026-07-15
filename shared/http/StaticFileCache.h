#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace pz::http
{

class StaticFileCache
{
public:
    struct Entry
    {
        std::string contentType;
        std::string body;
        std::string etag;
    };

    explicit StaticFileCache(std::string baseDir, bool reload = false);

    std::optional<Entry> get(const std::string& target) const;

private:
    static std::string loadFile(const std::string& path);
    static std::string contentType(const std::string& target);
    static std::string makeEtag(const std::string& body);
    static bool safeTarget(const std::string& target);
    static std::string normalize(const std::string& target);

    void preload();

    std::string m_baseDir;
    bool m_reload{false};
    std::unordered_map<std::string, Entry> m_files;
};

}
