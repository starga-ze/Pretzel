#include "service/auth/AuthService.h"

#include "config/Config.h"
#include "util/Logger.h"
#include "util/PasswordHash.h"

#include <chrono>
#include <random>
#include <sstream>

namespace pz::mgmtd
{

bool AuthService::loadCredential()
{
    // The admin credential lives hashed in the running-config (mgmtd.service.http.admin),
    // seeded/written by engined. mgmtd only reads it.
    const auto& http  = pz::config::Config::serviceSection("mgmtd", "http");
    const auto  admin = http.value("admin", nlohmann::json::object());

    const std::string hash = admin.value("password_hash", std::string{});
    if (!hash.empty())
    {
        m_username     = admin.value("username", std::string("admin"));
        m_passwordHash = hash;
        m_salt         = admin.value("salt", std::string{});
        m_mustChange   = admin.value("must_change", false);
        return true;
    }

    // No credential in config yet — engined seeds it during pre-flight, but it may not
    // have landed (e.g. the DB was briefly down at boot). Keep an in-memory hashed
    // default so logins aren't permanently broken; the default must be changed.
    const std::string salt = pz::util::generateSalt();

    LOG_WARN("AuthService: no admin credential in config yet — using in-memory default "
             "account 'admin' until engined seeds it");

    m_username     = "admin";
    m_passwordHash = pz::util::hashSha256(kDefaultPassword, salt);
    m_salt         = salt;
    m_mustChange   = true;
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
    if (m_passwordHash.empty() ||
        pz::util::hashSha256(password, m_salt) != m_passwordHash)
    {
        return {};
    }

    const auto sessionId = generateSessionId();

    m_sessions[sessionId] = Session{now() + m_sessionTtlSec};

    return LoginResult{true, sessionId, m_mustChange};
}

bool AuthService::checkPassword(const std::string& username,
                                const std::string& password) const
{
    if (username != m_username || m_passwordHash.empty())
    {
        return false;
    }
    return pz::util::hashSha256(password, m_salt) == m_passwordHash;
}

AuthService::Credential AuthService::makeCredential(const std::string& newPassword) const
{
    Credential cred;
    cred.salt         = pz::util::generateSalt();
    cred.passwordHash = pz::util::hashSha256(newPassword, cred.salt);
    return cred;
}

void AuthService::applyCredential(const std::string& passwordHash,
                                  const std::string& salt)
{
    m_passwordHash = passwordHash;
    m_salt         = salt;
    m_mustChange   = false;  // engined clears must_change in the same write
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

std::string AuthService::generateSessionId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << now() << dist(gen) << dist(gen);
    return oss.str();
}

} // namespace pz::mgmtd
