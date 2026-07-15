#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pz::mgmtd
{

class AuthService
{
public:
    struct LoginResult
    {
        bool success{false};
        std::string sessionId;
        bool mustChange{false};
    };

    AuthService() = default;

    struct Credential
    {
        std::string passwordHash;
        std::string salt;
    };

    bool loadCredential();

    LoginResult login(const std::string& username, const std::string& password);

    std::string createSsoSession(const std::string& username);

    bool checkPassword(const std::string& username, const std::string& password) const;

    Credential makeCredential(const std::string& newPassword) const;

    void applyCredential(const std::string& passwordHash, const std::string& salt);

    const std::string& username() const
    {
        return m_username;
    }

    bool mustChangePassword() const
    {
        return m_mustChange;
    }

    bool credentialLoaded() const
    {
        return m_loaded;
    }

    bool validateSession(const std::string& sessionId);
    void logout(const std::string& sessionId);

    std::string sessionUser(const std::string& sessionId) const;

private:
    struct Session
    {
        std::uint64_t expiresAt{0};
        std::string username;
    };

    static std::uint64_t now();
    static std::string generateSessionId();

private:
    std::unordered_map<std::string, Session> m_sessions;

    std::string m_username{"admin"};
    std::string m_passwordHash;
    std::string m_salt;
    bool m_mustChange{false};
    bool m_loaded{false};
    std::uint64_t m_sessionTtlSec{1800};
};

}
