#include "service/auth/AuthService.h"

#include "db/Database.h"
#include "util/Logger.h"
#include "util/PasswordHash.h"

#include <openssl/rand.h>

#include <algorithm>
#include <chrono>

namespace pz::mgmtd
{

bool AuthService::loadCredential()
{
    const auto rows =
        pz::db::Database::instance().queryRows("SELECT username, password_hash, salt, must_change FROM local_users "
                                               "WHERE username = 'admin' LIMIT 1");
    if (!rows.empty() && rows.front().size() >= 4)
    {
        m_username = rows.front()[0];
        m_passwordHash = rows.front()[1];
        m_salt = rows.front()[2];
        m_mustChange = (rows.front()[3] == "t");
        m_loaded = true;
        return true;
    }

    m_passwordHash.clear();
    m_salt.clear();
    m_mustChange = false;
    m_loaded = false;

    LOG_WARN("no readable local_users credential — refusing logins until "
             "it is available (retrying)");
    return false;
}

AuthService::LoginResult AuthService::login(const std::string& username, const std::string& password)
{
    // Checked before the username comparison so a blocked window cannot be probed by varying
    // the username, and before the hash so a blocked attempt costs nothing.
    if (now() < m_throttle.nextAllowedAt)
    {
        LoginResult blocked;
        blocked.throttled = true;
        return blocked;
    }

    if (username != m_username)
    {
        return {};
    }

    if (m_passwordHash.empty() || !pz::util::verifyPassword(password, m_salt, m_passwordHash))
    {
        noteLoginFailure();
        return {};
    }

    m_throttle = Throttle{};

    const auto sessionId = generateSessionId();
    if (sessionId.empty())
    {
        LOG_ERROR("session id generation failed — refusing the login");
        return {};
    }

    m_sessions[sessionId] = Session{now() + m_sessionTtlSec, m_username};

    LoginResult result;
    result.success = true;
    result.sessionId = sessionId;
    result.mustChange = m_mustChange;
    result.rehashNeeded = pz::util::needsRehash(m_passwordHash);
    return result;
}

void AuthService::noteLoginFailure()
{
    ++m_throttle.failures;

    if (m_throttle.failures <= kFreeAttempts)
    {
        return;
    }

    // Doubles per failure past the free allowance, capped — enough to make guessing
    // impractical without locking a fat-fingered operator out for the rest of the day.
    std::uint64_t delay = 1;
    for (int i = kFreeAttempts + 1; i < m_throttle.failures && delay < kMaxBackoffSec; ++i)
    {
        delay *= 2;
    }
    delay = std::min(delay, kMaxBackoffSec);

    m_throttle.nextAllowedAt = now() + delay;

    LOG_WARN("login throttled after {} consecutive failures (retry in {}s)", m_throttle.failures, delay);
}

std::string AuthService::createSsoSession(const std::string& username)
{
    const auto sessionId = generateSessionId();
    m_sessions[sessionId] = Session{now() + m_sessionTtlSec, username};
    return sessionId;
}

std::string AuthService::sessionUser(const std::string& sessionId) const
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || now() > it->second.expiresAt)
        return {};
    return it->second.username;
}

bool AuthService::checkPassword(const std::string& username, const std::string& password) const
{
    if (username != m_username || m_passwordHash.empty())
    {
        return false;
    }
    return pz::util::verifyPassword(password, m_salt, m_passwordHash);
}

AuthService::Credential AuthService::makeCredential(const std::string& newPassword) const
{
    Credential cred;
    cred.salt = pz::util::generateSalt();
    if (cred.salt.empty())
    {
        LOG_ERROR("salt generation failed — credential not created");
        return {};
    }

    cred.passwordHash = pz::util::hashPassword(newPassword, cred.salt);
    if (cred.passwordHash.empty())
    {
        LOG_ERROR("password hashing failed — credential not created");
        return {};
    }

    return cred;
}

void AuthService::applyCredential(const std::string& passwordHash, const std::string& salt)
{
    m_passwordHash = passwordHash;
    m_salt = salt;
    m_mustChange = false;
}

bool AuthService::validateSession(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return false;
    }

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
    {
        return false;
    }

    if (now() > it->second.expiresAt)
    {
        m_sessions.erase(it);
        return false;
    }

    return true;
}

void AuthService::logout(const std::string& sessionId)
{
    m_sessions.erase(sessionId);
}

std::uint64_t AuthService::now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 256 bits from the CSPRNG. The previous implementation seeded mt19937_64 from a single
// random_device draw, so despite emitting a long string the whole session space was the 32 bits
// of that seed — enumerable, and the timestamp prefix only narrowed it further. A session id is
// a bearer token: guessing one is the same as stealing it.
std::string AuthService::generateSessionId()
{
    unsigned char buf[32];
    if (RAND_bytes(buf, static_cast<int>(sizeof(buf))) != 1)
    {
        return {};
    }

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(sizeof(buf) * 2);
    for (unsigned char c : buf)
    {
        out.push_back(hex[(c >> 4) & 0xF]);
        out.push_back(hex[c & 0xF]);
    }
    return out;
}

}
