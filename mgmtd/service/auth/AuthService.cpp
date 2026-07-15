#include "service/auth/AuthService.h"

#include "db/Database.h"
#include "util/Logger.h"
#include "util/PasswordHash.h"

#include <chrono>
#include <random>
#include <sstream>

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
    if (username != m_username)
    {
        return {};
    }

    if (m_passwordHash.empty() || pz::util::hashSha256(password, m_salt) != m_passwordHash)
    {
        return {};
    }

    const auto sessionId = generateSessionId();

    m_sessions[sessionId] = Session{now() + m_sessionTtlSec, m_username};

    return LoginResult{true, sessionId, m_mustChange};
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
    return pz::util::hashSha256(password, m_salt) == m_passwordHash;
}

AuthService::Credential AuthService::makeCredential(const std::string& newPassword) const
{
    Credential cred;
    cred.salt = pz::util::generateSalt();
    cred.passwordHash = pz::util::hashSha256(newPassword, cred.salt);
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

std::string AuthService::generateSessionId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << now() << dist(gen) << dist(gen);
    return oss.str();
}

}
