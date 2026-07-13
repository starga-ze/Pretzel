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
        bool mustChange {false};  // session must change the password before anything else
    };

    AuthService() = default;

    struct Credential
    {
        std::string passwordHash;
        std::string salt;
    };

    // Loads the admin credential from the running-config (mgmtd.service.http.admin),
    // which engined seeds/writes. If no credential is present yet (e.g. the DB was
    // briefly down at boot before engined seeded it) it keeps an in-memory hashed
    // default so logins are not permanently broken, but writes nothing.
    bool loadCredential();

    LoginResult login(const std::string& username,
                      const std::string& password);

    // Mints a session for a user already authenticated out-of-band (SSO via authd).
    // No password is involved; returns the new session id (cookie value). SSO users
    // are never subject to the local forced-password-change flow.
    std::string createSsoSession(const std::string& username);

    // Verifies a username/password against the stored hash WITHOUT creating a session
    // (used to confirm the current password before a change).
    bool checkPassword(const std::string& username, const std::string& password) const;

    // Computes a fresh {hash, salt} for newPassword WITHOUT touching the DB or memory.
    // The caller forwards it to engined (the single writer) to persist.
    Credential makeCredential(const std::string& newPassword) const;

    // Adopts a credential into memory after handing the write to engined, so subsequent
    // logins verify against the new password and the forced-change gate opens.
    void applyCredential(const std::string& passwordHash, const std::string& salt);

    const std::string& username() const { return m_username; }

    // True while the admin still uses the factory-default password. The HTTP router
    // uses this to force a password change before allowing any other operation.
    bool mustChangePassword() const { return m_mustChange; }

    // True once a real credential row has been read from local_users. While false, the
    // account has an empty hash and login() refuses everything (fail-closed); the main
    // loop keeps retrying loadCredential() until it becomes true.
    bool credentialLoaded() const { return m_loaded; }

    bool validateSession(const std::string& sessionId);
    void logout(const std::string& sessionId);

    // Identifier of the user who owns a live session (local admin name, or the SSO
    // subject/email for a federated login). Empty when the session is unknown/expired.
    std::string sessionUser(const std::string& sessionId) const;

private:
    struct Session
    {
        std::uint64_t expiresAt {0};
        std::string   username;
    };

    static std::uint64_t now();
    static std::string generateSessionId();

private:
    std::unordered_map<std::string, Session> m_sessions;

    std::string m_username {"admin"};
    std::string m_passwordHash;
    std::string m_salt;
    bool m_mustChange {false};
    bool m_loaded {false};
    std::uint64_t m_sessionTtlSec {1800};
};

} // namespace pz::mgmtd
