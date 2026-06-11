#include "service/auth/AuthService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace pz::mgmtd
{

bool AuthService::loadFromDb(const std::string& defaultUsername)
{
    auto& db = pz::db::Database::instance();

    const auto rows =
        db.queryRows("SELECT username, password_hash, salt FROM admin_user LIMIT 1");
    if (!rows.empty() && rows.front().size() >= 3)
    {
        m_username     = rows.front()[0];
        m_passwordHash = rows.front()[1];
        m_salt         = rows.front()[2];
        return true;
    }

    // Factory-fresh: seed a default account (hashed) so the first login works without
    // a plaintext backdoor. Operator must change it via /api/change-password.
    const std::string username = defaultUsername.empty() ? "admin" : defaultUsername;
    const std::string salt     = generateSalt();
    const std::string hash     = hashSha256(kDefaultPassword, salt);

    const bool persisted = db.exec(
        "INSERT INTO admin_user (username, password_hash, salt) VALUES ($1, $2, $3) "
        "ON CONFLICT (username) DO NOTHING",
        {username, hash, salt});
    if (!persisted)
    {
        // DB write failed (e.g. PostgreSQL briefly down). Fall back to the in-memory
        // default — still hashed, no plaintext compare — so we are not locked out.
        LOG_WARN("AuthService: could not persist default admin account; using "
                 "in-memory default until DB is reachable");
    }

    LOG_WARN("AuthService: seeded default admin account '{}' with the default "
             "password — change it immediately via /api/change-password", username);

    m_username     = username;
    m_passwordHash = hash;
    m_salt         = salt;
    return true;
}

AuthService::LoginResult AuthService::login(const std::string& username,
                                            const std::string& password)
{
    if (username != m_username)
    {
        return {};
    }

    // No plaintext fallback: an unprovisioned account (empty hash) refuses all logins.
    if (m_passwordHash.empty() || hashSha256(password, m_salt) != m_passwordHash)
    {
        return {};
    }

    const auto sessionId = generateSessionId();

    m_sessions[sessionId] = Session{now() + m_sessionTtlSec};

    return LoginResult{true, sessionId};
}

bool AuthService::checkPassword(const std::string& username,
                                const std::string& password) const
{
    if (username != m_username || m_passwordHash.empty())
    {
        return false;
    }
    return hashSha256(password, m_salt) == m_passwordHash;
}

bool AuthService::changePassword(const std::string& username,
                                 const std::string& newPassword)
{
    if (username != m_username || newPassword.empty())
    {
        return false;
    }

    const std::string salt = generateSalt();
    const std::string hash = hashSha256(newPassword, salt);

    const bool ok = pz::db::Database::instance().exec(
        "UPDATE admin_user SET password_hash = $1, salt = $2, updated_at = now() "
        "WHERE username = $3",
        {hash, salt, username});
    if (!ok)
    {
        return false;
    }

    m_passwordHash = hash;
    m_salt         = salt;
    return true;
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
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string AuthService::hashSha256(const std::string& password,
                                    const std::string& salt)
{
    const std::string input = password + salt;

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);

    std::ostringstream oss;
    for (unsigned char byte : digest)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }

    return oss.str();
}

std::string AuthService::generateSessionId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << now() << dist(gen) << dist(gen);
    return oss.str();
}

std::string AuthService::generateSalt()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

} // namespace pz::mgmtd
