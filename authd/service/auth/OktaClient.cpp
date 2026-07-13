#include "service/auth/OktaClient.h"

#include "io/HttpsClient.h"   // pz::net::HttpsClient — see integration note (relocated from scand/api)
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace pz::authd
{

namespace
{

std::uint64_t nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string randomToken(std::size_t bytes)
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes * 2; ++i)
    {
        out.push_back(hex[dist(gen)]);
    }
    return out;
}

// ── base64url (no padding) ──────────────────────────────────────────────────
std::string base64UrlEncode(const unsigned char* data, std::size_t len)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    if (i < len)
    {
        std::uint32_t n = data[i] << 16;
        if (i + 1 < len) n |= data[i + 1] << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        if (i + 1 < len) out.push_back(tbl[(n >> 6) & 63]);
    }
    return out;
}

bool base64UrlDecode(const std::string& in, std::vector<unsigned char>& out)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in)
    {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

std::string sha256Raw(const std::string& in)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::string out;
    if (ctx &&
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(ctx, in.data(), in.size()) == 1 &&
        EVP_DigestFinal_ex(ctx, md, &mdLen) == 1)
    {
        out.assign(reinterpret_cast<char*>(md), mdLen);
    }
    if (ctx) EVP_MD_CTX_free(ctx);
    return out;
}

// Split "a.b.c" JWT into its three segments; returns false if not exactly 3 parts.
bool splitJwt(const std::string& jwt, std::string& h, std::string& p, std::string& s)
{
    auto d1 = jwt.find('.');
    if (d1 == std::string::npos) return false;
    auto d2 = jwt.find('.', d1 + 1);
    if (d2 == std::string::npos) return false;
    if (jwt.find('.', d2 + 1) != std::string::npos) return false;
    h = jwt.substr(0, d1);
    p = jwt.substr(d1 + 1, d2 - d1 - 1);
    s = jwt.substr(d2 + 1);
    return true;
}

} // namespace

void OktaClient::configure(const Config& cfg)
{
    m_cfg = cfg;
    m_txns.clear();
    if (m_cfg.enabled)
    {
        LOG_INFO("okta oidc enabled (issuer={}, client_id={})",
                 m_cfg.issuer, m_cfg.clientId);
    }
}

std::optional<OktaClient::Endpoint> OktaClient::parseIssuer(const std::string& issuer)
{
    // issuer := https://<host>[:port]<basePath>
    const std::string scheme = "https://";
    if (issuer.rfind(scheme, 0) != 0) return std::nullopt;
    std::string rest = issuer.substr(scheme.size());
    Endpoint ep;
    auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    ep.basePath = (slash == std::string::npos) ? "" : rest.substr(slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos)
    {
        ep.host = hostport;
        ep.port = 443;
    }
    else
    {
        ep.host = hostport.substr(0, colon);
        ep.port = static_cast<std::uint16_t>(std::stoi(hostport.substr(colon + 1)));
    }
    if (ep.host.empty()) return std::nullopt;
    // strip a trailing slash from basePath for consistent joining
    if (!ep.basePath.empty() && ep.basePath.back() == '/') ep.basePath.pop_back();
    return ep;
}

void OktaClient::pruneExpired(std::uint64_t now)
{
    for (auto it = m_txns.begin(); it != m_txns.end();)
    {
        it = (it->second.expiresAt < now) ? m_txns.erase(it) : std::next(it);
    }
}

OktaClient::StartResult OktaClient::buildAuthorizeUrl()
{
    StartResult r;
    if (!m_cfg.enabled) { r.error = "oidc disabled"; return r; }

    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep) { r.error = "bad issuer"; return r; }

    const std::uint64_t t = nowSec();
    pruneExpired(t);

    Txn txn;
    txn.nonce        = randomToken(16);
    txn.codeVerifier = randomToken(32);   // 64 hex chars — valid PKCE verifier
    txn.expiresAt    = t + m_cfg.txnTtlSec;
    const std::string state = randomToken(16);

    const std::string sha = sha256Raw(txn.codeVerifier);
    const std::string challenge =
        base64UrlEncode(reinterpret_cast<const unsigned char*>(sha.data()), sha.size());

    using HC = pz::net::HttpsClient;
    std::string url = "https://" + ep->host;
    if (ep->port != 443) url += ":" + std::to_string(ep->port);
    url += ep->basePath + "/v1/authorize"
         + "?client_id=" + HC::urlEncode(m_cfg.clientId)
         + "&response_type=code"
         + "&scope=" + HC::urlEncode(m_cfg.scopes)
         + "&redirect_uri=" + HC::urlEncode(m_cfg.redirectUri)
         + "&state=" + state
         + "&nonce=" + txn.nonce
         + "&code_challenge=" + challenge
         + "&code_challenge_method=S256";

    m_txns[state] = std::move(txn);

    r.success      = true;
    r.authorizeUrl = std::move(url);
    r.state        = state;
    return r;
}

OktaClient::Result OktaClient::exchangeAndVerify(const std::string& code,
                                                 const std::string& state)
{
    Result r;
    if (!m_cfg.enabled) { r.error = "oidc disabled"; return r; }

    const std::uint64_t t = nowSec();
    pruneExpired(t);

    auto it = m_txns.find(state);
    if (it == m_txns.end())
    {
        r.error = "unknown or expired state";
        return r;
    }
    const Txn txn = it->second;
    m_txns.erase(it);   // single-use

    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep) { r.error = "bad issuer"; return r; }

    using HC = pz::net::HttpsClient;
    const std::string body =
          "grant_type=authorization_code"
          "&code=" + HC::urlEncode(code) +
          "&redirect_uri=" + HC::urlEncode(m_cfg.redirectUri) +
          "&client_id=" + HC::urlEncode(m_cfg.clientId) +
          "&client_secret=" + HC::urlEncode(m_cfg.clientSecret) +
          "&code_verifier=" + txn.codeVerifier;

    HC::Request req;
    req.method      = "POST";
    req.host        = ep->host;
    req.port        = ep->port;
    req.target      = ep->basePath + "/v1/token";
    req.body        = body;
    req.contentType = "application/x-www-form-urlencoded";
    req.verifyTls   = m_cfg.verifyTls;
    req.timeoutMs   = m_cfg.timeoutMs;

    const auto resp = HC::send(req);
    if (!resp.ok)
    {
        r.error = "token endpoint failed: " +
                  (resp.error.empty() ? ("status " + std::to_string(resp.status)) : resp.error);
        return r;
    }

    std::string idToken;
    try
    {
        const auto j = nlohmann::json::parse(resp.body);
        idToken = j.value("id_token", "");
    }
    catch (const std::exception& e)
    {
        r.error = std::string("token response parse error: ") + e.what();
        return r;
    }
    if (idToken.empty()) { r.error = "no id_token in token response"; return r; }

    std::string email, err;
    if (!verifyIdToken(idToken, txn.nonce, email, err))
    {
        r.error = "id_token verification failed: " + err;
        return r;
    }

    r.success  = true;
    r.username = email;
    return r;
}

bool OktaClient::verifyIdToken(const std::string& idToken,
                               const std::string& expectedNonce,
                               std::string& emailOut,
                               std::string& errOut) const
{
    std::string h64, p64, s64;
    if (!splitJwt(idToken, h64, p64, s64)) { errOut = "malformed jwt"; return false; }

    std::vector<unsigned char> hBytes, pBytes;
    if (!base64UrlDecode(h64, hBytes) || !base64UrlDecode(p64, pBytes))
    {
        errOut = "base64url decode failed";
        return false;
    }

    std::string kid;
    try
    {
        const auto hdr = nlohmann::json::parse(std::string(hBytes.begin(), hBytes.end()));
        if (hdr.value("alg", "") != "RS256") { errOut = "unexpected alg"; return false; }
        kid = hdr.value("kid", "");
    }
    catch (const std::exception& e) { errOut = std::string("header parse: ") + e.what(); return false; }
    if (kid.empty()) { errOut = "no kid"; return false; }

    // Signature first (fail closed) — the signing input is the raw "header.payload".
    if (!verifySignatureRs256(h64 + "." + p64, s64, kid, errOut))
    {
        return false;   // errOut set by callee
    }

    // Claims.
    try
    {
        const auto claims = nlohmann::json::parse(std::string(pBytes.begin(), pBytes.end()));
        const std::uint64_t now = nowSec();

        if (claims.value("iss", "") != m_cfg.issuer) { errOut = "iss mismatch"; return false; }

        // aud may be a string or an array; require clientId to be present.
        bool audOk = false;
        if (claims.contains("aud"))
        {
            const auto& aud = claims["aud"];
            if (aud.is_string()) audOk = (aud.get<std::string>() == m_cfg.clientId);
            else if (aud.is_array())
                for (const auto& a : aud)
                    if (a.is_string() && a.get<std::string>() == m_cfg.clientId) audOk = true;
        }
        if (!audOk) { errOut = "aud mismatch"; return false; }

        if (claims.value("exp", std::uint64_t{0}) <= now) { errOut = "expired"; return false; }
        if (claims.contains("nbf") && claims["nbf"].get<std::uint64_t>() > now + 60)
        { errOut = "not yet valid"; return false; }

        if (!expectedNonce.empty() && claims.value("nonce", "") != expectedNonce)
        { errOut = "nonce mismatch"; return false; }

        emailOut = claims.value("email", "");
        if (emailOut.empty()) emailOut = claims.value("preferred_username", "");
        if (emailOut.empty()) emailOut = claims.value("sub", "");
        if (emailOut.empty()) { errOut = "no email/sub claim"; return false; }
    }
    catch (const std::exception& e) { errOut = std::string("claims parse: ") + e.what(); return false; }

    return true;
}

bool OktaClient::verifySignatureRs256(const std::string& signingInput,
                                      const std::string& signatureB64Url,
                                      const std::string& kid,
                                      std::string& errOut) const
{
    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep) { errOut = "bad issuer"; return false; }

    // Fetch JWKS. TODO(perf): cache by kid with a short TTL instead of per-login.
    using HC = pz::net::HttpsClient;
    const auto resp = HC::get(ep->host, ep->port, ep->basePath + "/v1/keys",
                              m_cfg.timeoutMs, m_cfg.verifyTls);
    if (!resp.ok) { errOut = "jwks fetch failed"; return false; }

    std::string nB64, eB64;
    try
    {
        const auto jwks = nlohmann::json::parse(resp.body);
        for (const auto& k : jwks.value("keys", nlohmann::json::array()))
        {
            if (k.value("kid", "") == kid && k.value("kty", "") == "RSA")
            {
                nB64 = k.value("n", "");
                eB64 = k.value("e", "");
                break;
            }
        }
    }
    catch (const std::exception& e) { errOut = std::string("jwks parse: ") + e.what(); return false; }
    if (nB64.empty() || eB64.empty()) { errOut = "kid not found in jwks"; return false; }

    std::vector<unsigned char> nBytes, eBytes, sigBytes;
    if (!base64UrlDecode(nB64, nBytes) || !base64UrlDecode(eB64, eBytes) ||
        !base64UrlDecode(signatureB64Url, sigBytes))
    { errOut = "jwks/sig decode failed"; return false; }

    // Build an RSA public key from (n, e) using the OpenSSL 3.0 provider API.
    bool ok = false;
    EVP_PKEY* pkey = nullptr;
    OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
    BIGNUM* bn_n = BN_bin2bn(nBytes.data(), static_cast<int>(nBytes.size()), nullptr);
    BIGNUM* bn_e = BN_bin2bn(eBytes.data(), static_cast<int>(eBytes.size()), nullptr);
    OSSL_PARAM* params = nullptr;
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_MD_CTX* mdctx = nullptr;

    if (bld && bn_n && bn_e &&
        OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) &&
        OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e) &&
        (params = OSSL_PARAM_BLD_to_param(bld)) &&
        (pctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)) &&
        EVP_PKEY_fromdata_init(pctx) == 1 &&
        EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) == 1)
    {
        mdctx = EVP_MD_CTX_new();
        if (mdctx &&
            EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
            EVP_DigestVerify(mdctx,
                             sigBytes.data(), sigBytes.size(),
                             reinterpret_cast<const unsigned char*>(signingInput.data()),
                             signingInput.size()) == 1)
        {
            ok = true;
        }
    }

    if (mdctx) EVP_MD_CTX_free(mdctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (pctx) EVP_PKEY_CTX_free(pctx);
    if (params) OSSL_PARAM_free(params);
    if (bld) OSSL_PARAM_BLD_free(bld);
    if (bn_n) BN_free(bn_n);
    if (bn_e) BN_free(bn_e);

    if (!ok) errOut = "rs256 signature invalid";
    return ok;
}

} // namespace pz::authd
