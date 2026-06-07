#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pz::mgmtd
{

class AuthService
{
public:
    struct LoginResult
    {
        bool success {false};
        std::string sessionId;
    };

    AuthService() = default;

    bool load(const std::string& username,
              const std::string& passwordHash,
              const std::string& salt);

    LoginResult login(const std::string& username,
                      const std::string& password);

    bool validateSession(const std::string& sessionId);
    void logout(const std::string& sessionId);

private:
    struct Session
    {
        std::uint64_t expiresAt {0};
    };

    static std::uint64_t now();
    static std::string hashSha256(const std::string& password,
                                  const std::string& salt);
    static std::string generateSessionId();

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, Session> m_sessions;

    std::string m_username {"admin"};
    std::string m_passwordHash;
    std::string m_salt;
    std::uint64_t m_sessionTtlSec {1800};
};

} // namespace pz::mgmtd
