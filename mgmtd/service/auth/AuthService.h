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
        bool success {false};
        std::string sessionId;
    };

    AuthService() = default;

    // Loads the admin credential from the admin_user DB table. On a factory-fresh
    // device (no row yet) it seeds a default account — username = defaultUsername,
    // password = kDefaultPassword — stored hashed, and returns true. The plaintext
    // password is never stored; the default MUST be changed via changePassword().
    bool loadFromDb(const std::string& defaultUsername = "admin");

    LoginResult login(const std::string& username,
                      const std::string& password);

    // Verifies a username/password against the stored hash WITHOUT creating a session
    // (used to confirm the current password before a change).
    bool checkPassword(const std::string& username, const std::string& password) const;

    // Re-hashes newPassword with a fresh salt and persists it to admin_user. Returns
    // false if the username is unknown, the password is empty, or the DB write fails.
    bool changePassword(const std::string& username, const std::string& newPassword);

    const std::string& username() const { return m_username; }

    bool validateSession(const std::string& sessionId);
    void logout(const std::string& sessionId);

private:
    struct Session
    {
        std::uint64_t expiresAt {0};
    };

    // Default password seeded on a factory-fresh device (hashed at seed time). Change
    // it immediately via /api/change-password — there is no plaintext backdoor.
    static constexpr const char* kDefaultPassword = "admin";

    static std::uint64_t now();
    static std::string hashSha256(const std::string& password,
                                  const std::string& salt);
    static std::string generateSessionId();
    static std::string generateSalt();

private:
    std::unordered_map<std::string, Session> m_sessions;

    std::string m_username {"admin"};
    std::string m_passwordHash;
    std::string m_salt;
    std::uint64_t m_sessionTtlSec {1800};
};

} // namespace pz::mgmtd
