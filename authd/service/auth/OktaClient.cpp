#include "service/auth/OktaClient.h"

#include "algorithm/Base64.h"
#include "algorithm/Hex.h"
#include "http/HttpClient.h"
#include "http/UrlEncode.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace pz::authd
{

namespace
{

// Okta is reached over a public CA, so the shared client verifies the chain and hostname instead
// of pinning. Its outcome is split across three fields — the transport result, whether the request
// was written at all, and the HTTP status — so fold them back into the single "did this work"
// question the two call sites below ask.
pz::http::ClientRequest idpRequest(const std::string& host, std::uint16_t port, const std::string& target,
                                   int timeoutMs, bool verifyTls)
{
    pz::http::ClientRequest req;
    req.host = host;
    req.port = port;
    req.target = target;
    req.verifyCa = verifyTls;
    req.timeout = std::chrono::milliseconds(timeoutMs);
    req.headers.emplace_back("User-Agent", "pretzel-authd/1.0");
    req.headers.emplace_back("Accept", "application/json");
    return req;
}

bool succeeded(const pz::http::ClientResponse& res)
{
    return res.requestSent && res.status >= 200 && res.status < 300;
}

std::string failureText(const pz::http::ClientResponse& res)
{
    if (!res.error.empty())
        return res.error;
    if (!res.tlsOk)
        return "TLS handshake failed";
    if (!res.requestSent)
        return "server certificate rejected";
    return "status " + std::to_string(res.status);
}

std::uint64_t nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}




std::string sha256Raw(const std::string& in)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::string out;
    if (ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 && EVP_DigestUpdate(ctx, in.data(), in.size()) == 1 &&
        EVP_DigestFinal_ex(ctx, md, &mdLen) == 1)
    {
        out.assign(reinterpret_cast<char*>(md), mdLen);
    }
    if (ctx)
        EVP_MD_CTX_free(ctx);
    return out;
}

bool splitJwt(const std::string& jwt, std::string& h, std::string& p, std::string& s)
{
    auto d1 = jwt.find('.');
    if (d1 == std::string::npos)
        return false;
    auto d2 = jwt.find('.', d1 + 1);
    if (d2 == std::string::npos)
        return false;
    if (jwt.find('.', d2 + 1) != std::string::npos)
        return false;
    h = jwt.substr(0, d1);
    p = jwt.substr(d1 + 1, d2 - d1 - 1);
    s = jwt.substr(d2 + 1);
    return true;
}

}

void OktaClient::configure(const Config& cfg)
{
    m_cfg = cfg;
    m_txns.clear();
    if (m_cfg.enabled)
    {
        LOG_INFO("okta oidc enabled (issuer={}, client_id={})", m_cfg.issuer, m_cfg.clientId);
    }
}

std::optional<OktaClient::Endpoint> OktaClient::parseIssuer(const std::string& issuer)
{
    const std::string scheme = "https://";
    if (issuer.rfind(scheme, 0) != 0)
        return std::nullopt;
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
    if (ep.host.empty())
        return std::nullopt;
    if (!ep.basePath.empty() && ep.basePath.back() == '/')
        ep.basePath.pop_back();
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
    if (!m_cfg.enabled)
    {
        r.error = "oidc disabled";
        return r;
    }

    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep)
    {
        r.error = "bad issuer";
        return r;
    }

    const std::uint64_t t = nowSec();
    pruneExpired(t);

    Txn txn;
    txn.nonce = pz::algorithm::randomHex(16);
    txn.codeVerifier = pz::algorithm::randomHex(32);
    txn.expiresAt = t + m_cfg.txnTtlSec;
    const std::string state = pz::algorithm::randomHex(16);

    // All three are bearer-grade: state is the CSRF binding, nonce ties the id_token to this
    // request, and the verifier is the PKCE secret. randomHex answers empty when the entropy
    // source fails, and issuing an empty one would be issuing a credential everybody can guess.
    if (txn.nonce.empty() || txn.codeVerifier.empty() || state.empty())
    {
        r.error = "no entropy available to start a login";
        return r;
    }

    const std::string sha = sha256Raw(txn.codeVerifier);
    const std::string challenge = pz::algorithm::base64Encode(sha.data(), sha.size(),
                                                              pz::algorithm::Base64Alphabet::UrlSafe,
                                                              /*pad=*/false);

    using pz::http::urlEncode;
    std::string url = "https://" + ep->host;
    if (ep->port != 443)
        url += ":" + std::to_string(ep->port);
    url += ep->basePath + "/v1/authorize" + "?client_id=" + urlEncode(m_cfg.clientId) + "&response_type=code" +
           "&scope=" + urlEncode(m_cfg.scopes) + "&redirect_uri=" + urlEncode(m_cfg.redirectUri) +
           "&state=" + state + "&nonce=" + txn.nonce + "&code_challenge=" + challenge + "&code_challenge_method=S256";

    m_txns[state] = std::move(txn);

    r.success = true;
    r.authorizeUrl = std::move(url);
    r.state = state;
    return r;
}

OktaClient::Result OktaClient::exchangeAndVerify(const std::string& code, const std::string& state)
{
    Result r;
    if (!m_cfg.enabled)
    {
        r.error = "oidc disabled";
        return r;
    }

    const std::uint64_t t = nowSec();
    pruneExpired(t);

    auto it = m_txns.find(state);
    if (it == m_txns.end())
    {
        r.error = "unknown or expired state";
        return r;
    }
    const Txn txn = it->second;
    m_txns.erase(it);

    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep)
    {
        r.error = "bad issuer";
        return r;
    }

    using pz::http::urlEncode;
    const std::string body = "grant_type=authorization_code"
                             "&code=" +
                             urlEncode(code) + "&redirect_uri=" + urlEncode(m_cfg.redirectUri) +
                             "&client_id=" + urlEncode(m_cfg.clientId) +
                             "&client_secret=" + urlEncode(m_cfg.clientSecret) +
                             "&code_verifier=" + txn.codeVerifier;

    auto req = idpRequest(ep->host, ep->port, ep->basePath + "/v1/token", m_cfg.timeoutMs, m_cfg.verifyTls);
    req.method = "POST";
    req.body = body;
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");

    const auto resp = pz::http::requestSync(std::move(req));
    if (!succeeded(resp))
    {
        r.error = "token endpoint failed: " + failureText(resp);
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
    if (idToken.empty())
    {
        r.error = "no id_token in token response";
        return r;
    }

    std::string email, err;
    if (!verifyIdToken(idToken, txn.nonce, email, err))
    {
        r.error = "id_token verification failed: " + err;
        return r;
    }

    r.success = true;
    r.username = email;
    return r;
}

bool OktaClient::verifyIdToken(const std::string& idToken, const std::string& expectedNonce, std::string& emailOut,
                               std::string& errOut) const
{
    std::string h64, p64, s64;
    if (!splitJwt(idToken, h64, p64, s64))
    {
        errOut = "malformed jwt";
        return false;
    }

    std::vector<unsigned char> hBytes, pBytes;
    if (!pz::algorithm::base64Decode(h64, hBytes) || !pz::algorithm::base64Decode(p64, pBytes))
    {
        errOut = "base64url decode failed";
        return false;
    }

    std::string kid;
    try
    {
        const auto hdr = nlohmann::json::parse(std::string(hBytes.begin(), hBytes.end()));
        if (hdr.value("alg", "") != "RS256")
        {
            errOut = "unexpected alg";
            return false;
        }
        kid = hdr.value("kid", "");
    }
    catch (const std::exception& e)
    {
        errOut = std::string("header parse: ") + e.what();
        return false;
    }
    if (kid.empty())
    {
        errOut = "no kid";
        return false;
    }

    if (!verifySignatureRs256(h64 + "." + p64, s64, kid, errOut))
    {
        return false;
    }

    try
    {
        const auto claims = nlohmann::json::parse(std::string(pBytes.begin(), pBytes.end()));
        const std::uint64_t now = nowSec();

        if (claims.value("iss", "") != m_cfg.issuer)
        {
            errOut = "iss mismatch";
            return false;
        }

        bool audOk = false;
        if (claims.contains("aud"))
        {
            const auto& aud = claims["aud"];
            if (aud.is_string())
                audOk = (aud.get<std::string>() == m_cfg.clientId);
            else if (aud.is_array())
                for (const auto& a : aud)
                    if (a.is_string() && a.get<std::string>() == m_cfg.clientId)
                        audOk = true;
        }
        if (!audOk)
        {
            errOut = "aud mismatch";
            return false;
        }

        if (claims.value("exp", std::uint64_t{0}) <= now)
        {
            errOut = "expired";
            return false;
        }
        if (claims.contains("nbf") && claims["nbf"].get<std::uint64_t>() > now + 60)
        {
            errOut = "not yet valid";
            return false;
        }

        if (!expectedNonce.empty() && claims.value("nonce", "") != expectedNonce)
        {
            errOut = "nonce mismatch";
            return false;
        }

        emailOut = claims.value("email", "");
        if (emailOut.empty())
            emailOut = claims.value("preferred_username", "");
        if (emailOut.empty())
            emailOut = claims.value("sub", "");
        if (emailOut.empty())
        {
            errOut = "no email/sub claim";
            return false;
        }
    }
    catch (const std::exception& e)
    {
        errOut = std::string("claims parse: ") + e.what();
        return false;
    }

    return true;
}

bool OktaClient::verifySignatureRs256(const std::string& signingInput, const std::string& signatureB64Url,
                                      const std::string& kid, std::string& errOut) const
{
    auto ep = parseIssuer(m_cfg.issuer);
    if (!ep)
    {
        errOut = "bad issuer";
        return false;
    }

    const auto resp = pz::http::requestSync(
        idpRequest(ep->host, ep->port, ep->basePath + "/v1/keys", m_cfg.timeoutMs, m_cfg.verifyTls));
    if (!succeeded(resp))
    {
        errOut = "jwks fetch failed: " + failureText(resp);
        return false;
    }

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
    catch (const std::exception& e)
    {
        errOut = std::string("jwks parse: ") + e.what();
        return false;
    }
    if (nB64.empty() || eB64.empty())
    {
        errOut = "kid not found in jwks";
        return false;
    }

    std::vector<unsigned char> nBytes, eBytes, sigBytes;
    if (!pz::algorithm::base64Decode(nB64, nBytes) || !pz::algorithm::base64Decode(eB64, eBytes) || !pz::algorithm::base64Decode(signatureB64Url, sigBytes))
    {
        errOut = "jwks/sig decode failed";
        return false;
    }

    bool ok = false;
    EVP_PKEY* pkey = nullptr;
    OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
    BIGNUM* bn_n = BN_bin2bn(nBytes.data(), static_cast<int>(nBytes.size()), nullptr);
    BIGNUM* bn_e = BN_bin2bn(eBytes.data(), static_cast<int>(eBytes.size()), nullptr);
    OSSL_PARAM* params = nullptr;
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_MD_CTX* mdctx = nullptr;

    if (bld && bn_n && bn_e && OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) &&
        OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e) && (params = OSSL_PARAM_BLD_to_param(bld)) &&
        (pctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)) && EVP_PKEY_fromdata_init(pctx) == 1 &&
        EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) == 1)
    {
        mdctx = EVP_MD_CTX_new();
        if (mdctx && EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
            EVP_DigestVerify(mdctx, sigBytes.data(), sigBytes.size(),
                             reinterpret_cast<const unsigned char*>(signingInput.data()), signingInput.size()) == 1)
        {
            ok = true;
        }
    }

    if (mdctx)
        EVP_MD_CTX_free(mdctx);
    if (pkey)
        EVP_PKEY_free(pkey);
    if (pctx)
        EVP_PKEY_CTX_free(pctx);
    if (params)
        OSSL_PARAM_free(params);
    if (bld)
        OSSL_PARAM_BLD_free(bld);
    if (bn_n)
        BN_free(bn_n);
    if (bn_e)
        BN_free(bn_e);

    if (!ok)
        errOut = "rs256 signature invalid";
    return ok;
}

}
