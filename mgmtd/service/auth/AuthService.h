#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class AuthEvent;

class AuthService
{
public:
    // The one asynchronous part of this domain: authd verified a SAML assertion and answered on the
    // ticket the browser is polling. Everything else here answers inside the request that asked.
    void handleEvent(MgmtdServiceManager& serviceManager, const AuthEvent& event);

    struct LoginResult
    {
        bool success{false};
        std::string sessionId;
        bool mustChange{false};
        // The stored credential verified, but in an outdated format. The caller has the
        // plaintext at exactly this moment and nowhere else, so this is the only opportunity to
        // re-store it — see WebService::handleLogin.
        bool rehashNeeded{false};
        // Refused before the password was even checked, because too many attempts failed
        // recently. Distinguished from a wrong password so the UI can say so.
        bool throttled{false};
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

    std::uint64_t sessionTtlSec() const
    {
        return m_sessionTtlSec;
    }

    // Checks a session without touching its lifetime. Every authenticated request goes through
    // this, and most of them are not the operator doing anything — the Home dashboard, the
    // Infrastructure and API Collection live views and the log tail all poll on timers. If those
    // extended the session, leaving a browser tab open on any of them would keep it alive for
    // ever and the timeout would mean nothing.
    bool validateSession(const std::string& sessionId);

    // Extends a live session by the full TTL. Called only from the keepalive route, which the
    // frontend fires only after real operator input (see the heartbeat in www/js/main.js) — that
    // is what makes the TTL an idle timeout rather than a fixed session lifetime.
    bool renewSession(const std::string& sessionId);

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

    // Password verification is deliberately expensive (PBKDF2), and this daemon runs one
    // cooperative loop — so an unauthenticated caller guessing in a loop would both brute-force
    // the password and stall the daemon. Consecutive failures against a known username back
    // off; a correct password clears the counter. Unknown usernames never reach the hash, so
    // they cost nothing and need no throttle.
    struct Throttle
    {
        int failures{0};
        std::uint64_t nextAllowedAt{0};
    };

    static constexpr int kFreeAttempts = 5;
    static constexpr std::uint64_t kMaxBackoffSec = 30;

    void noteLoginFailure();

private:
    std::unordered_map<std::string, Session> m_sessions;
    Throttle m_throttle;

    std::string m_username{"admin"};
    std::string m_passwordHash;
    std::string m_salt;
    bool m_mustChange{false};
    bool m_loaded{false};
    // Idle timeout: 30 minutes with no operator input. Set at login and pushed forward by
    // renewSession(); nothing else moves it.
    std::uint64_t m_sessionTtlSec{1800};
};

}
