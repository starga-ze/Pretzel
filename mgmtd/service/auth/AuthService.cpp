#include "service/auth/AuthService.h"

#include <openssl/sha.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace nf::mgmtd
{

bool AuthService::load(const std::string& username,
                       const std::string& passwordHash,
                       const std::string& salt)
{
    m_username = username.empty() ? "admin" : username;
    m_passwordHash = passwordHash;
    m_salt = salt;
    return true;
}

AuthService::LoginResult AuthService::login(const std::string& username,
                                            const std::string& password)
{
    if (username != m_username)
    {
        return {};
    }

    if (!m_passwordHash.empty() && hashSha256(password, m_salt) != m_passwordHash)
    {
        return {};
    }

    // Development fallback. Once config has password_hash, plain admin/admin no longer applies.
    if (m_passwordHash.empty() && password != "admin")
    {
        return {};
    }

    const auto sessionId = generateSessionId();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions[sessionId] = Session{now() + m_sessionTtlSec};

    return LoginResult{true, sessionId};
}

bool AuthService::validateSession(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

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
    std::lock_guard<std::mutex> lock(m_mutex);
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

} // namespace nf::mgmtd
