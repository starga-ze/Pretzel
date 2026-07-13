#include "service/auth/SamlClient.h"

#include "io/HttpsClient.h"   // pz::net::HttpsClient::urlEncode
#include "util/Logger.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/xmldsig.h>
#include <xmlsec/crypto.h>
#include <xmlsec/keysmngr.h>

#include <zlib.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

namespace pz::authd
{

namespace
{

constexpr const char* kNsProtocol  = "urn:oasis:names:tc:SAML:2.0:protocol";
constexpr const char* kNsAssertion = "urn:oasis:names:tc:SAML:2.0:assertion";
constexpr const char* kStatusOk    = "urn:oasis:names:tc:SAML:2.0:status:Success";

std::uint64_t nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string randomId()
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s = "_";
    for (int i = 0; i < 32; ++i) s.push_back(hex[dist(gen)]);
    return s;
}

std::string utcNow()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ── base64 (standard alphabet) ──────────────────────────────────────────────
std::string base64Encode(const unsigned char* data, std::size_t len)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool base64Decode(const std::string& in, std::vector<unsigned char>& out)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in)
    {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((buf >> bits) & 0xFF); }
    }
    return true;
}

// Raw DEFLATE (no zlib header) for the SAML HTTP-Redirect binding.
bool rawDeflate(const std::string& in, std::vector<unsigned char>& out)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    unsigned char chunk[4096];
    int ret;
    do {
        zs.next_out  = chunk;
        zs.avail_out = sizeof(chunk);
        ret = deflate(&zs, Z_FINISH);
        out.insert(out.end(), chunk, chunk + (sizeof(chunk) - zs.avail_out));
    } while (ret == Z_OK);
    deflateEnd(&zs);
    return ret == Z_STREAM_END;
}

// ── libxml2 tree helpers (namespace-aware by local name) ───────────────────
bool localIs(xmlNodePtr n, const char* name)
{
    return n && n->type == XML_ELEMENT_NODE && n->name &&
           std::strcmp(reinterpret_cast<const char*>(n->name), name) == 0;
}

xmlNodePtr childByLocal(xmlNodePtr parent, const char* name)
{
    if (!parent) return nullptr;
    for (xmlNodePtr c = parent->children; c; c = c->next)
        if (localIs(c, name)) return c;
    return nullptr;
}

std::string nodeText(xmlNodePtr n)
{
    if (!n) return {};
    xmlChar* t = xmlNodeGetContent(n);
    std::string s = t ? reinterpret_cast<const char*>(t) : "";
    if (t) xmlFree(t);
    // trim surrounding whitespace
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

std::string attr(xmlNodePtr n, const char* name)
{
    if (!n) return {};
    xmlChar* v = xmlGetProp(n, reinterpret_cast<const xmlChar*>(name));
    std::string s = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return s;
}

// Register every element's ID="..." attribute so xmlsec can resolve URI="#id".
void registerIds(xmlNodePtr node)
{
    for (xmlNodePtr n = node; n; n = n->next)
    {
        if (n->type == XML_ELEMENT_NODE)
        {
            xmlAttrPtr a = xmlHasProp(n, reinterpret_cast<const xmlChar*>("ID"));
            if (a)
            {
                xmlChar* id = xmlNodeListGetString(n->doc, a->children, 1);
                if (id) { xmlAddID(nullptr, n->doc, id, a); xmlFree(id); }
            }
            registerIds(n->children);
        }
    }
}

// Find an <Assertion> that directly contains a <Signature>; returns the signature node.
xmlNodePtr findSignedAssertion(xmlNodePtr root, xmlNodePtr& assertionOut)
{
    // SAMLResponse root is samlp:Response; assertion is a direct child.
    for (xmlNodePtr c = root->children; c; c = c->next)
    {
        if (localIs(c, "Assertion"))
        {
            xmlNodePtr sig = childByLocal(c, "Signature");
            if (sig) { assertionOut = c; return sig; }
        }
    }
    return nullptr;
}

std::time_t parseIso8601(const std::string& s)
{
    std::tm tm{};
    if (!strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) return 0;
    return timegm(&tm);   // SAML instants are UTC (trailing Z)
}

} // namespace

void SamlClient::configure(const Config& cfg)
{
    m_cfg = cfg;
    if (m_cfg.enabled)
    {
        LOG_INFO("saml enabled (idp={}, sp={})", m_cfg.idpEntityId, m_cfg.spEntityId);
        ensureXmlSecInit();
    }
}

bool SamlClient::ensureXmlSecInit()
{
    static bool ok = [] {
        xmlInitParser();
        LIBXML_TEST_VERSION
        if (xmlSecInit() < 0) { LOG_ERROR("xmlsec init failed"); return false; }
        if (xmlSecCryptoAppInit(nullptr) < 0) { LOG_ERROR("xmlsec crypto app init failed"); return false; }
        if (xmlSecCryptoInit() < 0) { LOG_ERROR("xmlsec crypto init failed"); return false; }
        return true;
    }();
    return ok;
}

SamlClient::StartResult SamlClient::buildAuthnRedirectUrl(const std::string& relayState)
{
    StartResult r;
    if (!m_cfg.enabled) { r.error = "saml disabled"; return r; }

    r.requestId = randomId();
    const std::string xml =
        "<samlp:AuthnRequest xmlns:samlp=\"" + std::string(kNsProtocol) + "\""
        " xmlns:saml=\"" + std::string(kNsAssertion) + "\""
        " ID=\"" + r.requestId + "\" Version=\"2.0\""
        " IssueInstant=\"" + utcNow() + "\""
        " Destination=\"" + m_cfg.idpSsoUrl + "\""
        " ProtocolBinding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\""
        " AssertionConsumerServiceURL=\"" + m_cfg.acsUrl + "\">"
        "<saml:Issuer>" + m_cfg.spEntityId + "</saml:Issuer>"
        "</samlp:AuthnRequest>";

    std::vector<unsigned char> deflated;
    if (!rawDeflate(xml, deflated)) { r.error = "deflate failed"; return r; }

    using HC = pz::net::HttpsClient;
    const std::string enc = HC::urlEncode(base64Encode(deflated.data(), deflated.size()));

    std::string url = m_cfg.idpSsoUrl;
    url += (url.find('?') == std::string::npos) ? '?' : '&';
    url += "SAMLRequest=" + enc;
    if (!relayState.empty()) url += "&RelayState=" + HC::urlEncode(relayState);

    r.success     = true;
    r.redirectUrl = std::move(url);
    return r;
}

bool SamlClient::verifySignature(void* docV, void*& signedAssertion, std::string& errOut) const
{
    xmlDocPtr  doc  = static_cast<xmlDocPtr>(docV);
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) { errOut = "no root"; return false; }

    registerIds(root);

    xmlNodePtr assertion = nullptr;
    xmlNodePtr sig = findSignedAssertion(root, assertion);
    if (!sig) { errOut = "no signed assertion"; return false; }

    xmlSecKeysMngrPtr mngr = xmlSecKeysMngrCreate();
    if (!mngr || xmlSecCryptoAppDefaultKeysMngrInit(mngr) < 0)
    {
        if (mngr) xmlSecKeysMngrDestroy(mngr);
        errOut = "keys mngr init failed";
        return false;
    }

    bool ok = false;
    if (xmlSecCryptoAppKeysMngrCertLoadMemory(
            mngr,
            reinterpret_cast<const xmlSecByte*>(m_cfg.idpCertPem.data()),
            static_cast<xmlSecSize>(m_cfg.idpCertPem.size()),
            xmlSecKeyDataFormatPem,
            xmlSecKeyDataTypeTrusted) < 0)
    {
        errOut = "load idp cert failed";
    }
    else
    {
        xmlSecDSigCtxPtr dsig = xmlSecDSigCtxCreate(mngr);
        if (!dsig)
        {
            errOut = "dsig ctx create failed";
        }
        else
        {
            if (xmlSecDSigCtxVerify(dsig, sig) < 0)
            {
                errOut = "signature verify error";
            }
            else if (dsig->status != xmlSecDSigStatusSucceeded)
            {
                errOut = "signature invalid";
            }
            else
            {
                ok = true;
                signedAssertion = assertion;   // read attributes ONLY from here
            }
            xmlSecDSigCtxDestroy(dsig);
        }
    }

    xmlSecKeysMngrDestroy(mngr);
    return ok;
}

SamlClient::Result SamlClient::verifyResponse(const std::string& base64SamlResponse)
{
    Result r;
    if (!m_cfg.enabled) { r.error = "saml disabled"; return r; }
    if (!ensureXmlSecInit()) { r.error = "xmlsec unavailable"; return r; }

    std::vector<unsigned char> xml;
    if (!base64Decode(base64SamlResponse, xml) || xml.empty())
    {
        r.error = "bad base64 SAMLResponse";
        return r;
    }

    xmlDocPtr doc = xmlReadMemory(reinterpret_cast<const char*>(xml.data()),
                                  static_cast<int>(xml.size()), "saml.xml", nullptr,
                                  XML_PARSE_NONET | XML_PARSE_NOENT);
    if (!doc) { r.error = "xml parse failed"; return r; }

    // Signature first (fail closed), then read attributes only from the signed assertion.
    void* signedAssertionV = nullptr;
    std::string err;
    if (!verifySignature(doc, signedAssertionV, err))
    {
        xmlFreeDoc(doc);
        r.error = err;
        return r;
    }
    xmlNodePtr assertion = static_cast<xmlNodePtr>(signedAssertionV);
    xmlNodePtr root      = xmlDocGetRootElement(doc);

    // Response status == Success.
    if (xmlNodePtr status = childByLocal(root, "Status"))
    {
        if (attr(childByLocal(status, "StatusCode"), "Value") != kStatusOk)
        {
            xmlFreeDoc(doc); r.error = "status not success"; return r;
        }
    }

    // Issuer of the signed assertion must match the configured IdP.
    if (nodeText(childByLocal(assertion, "Issuer")) != m_cfg.idpEntityId)
    {
        xmlFreeDoc(doc); r.error = "issuer mismatch"; return r;
    }

    // Conditions: time window + audience.
    const std::uint64_t now  = nowSec();
    const std::uint64_t skew = m_cfg.clockSkewSec;
    if (xmlNodePtr cond = childByLocal(assertion, "Conditions"))
    {
        const std::string nb = attr(cond, "NotBefore");
        const std::string na = attr(cond, "NotOnOrAfter");
        if (!nb.empty() && static_cast<std::uint64_t>(parseIso8601(nb)) > now + skew)
        { xmlFreeDoc(doc); r.error = "assertion not yet valid"; return r; }
        if (!na.empty() && now > static_cast<std::uint64_t>(parseIso8601(na)) + skew)
        { xmlFreeDoc(doc); r.error = "assertion expired"; return r; }

        bool audOk = m_cfg.spEntityId.empty();
        if (xmlNodePtr ar = childByLocal(cond, "AudienceRestriction"))
            for (xmlNodePtr a = ar->children; a; a = a->next)
                if (localIs(a, "Audience") && nodeText(a) == m_cfg.spEntityId) audOk = true;
        if (!audOk) { xmlFreeDoc(doc); r.error = "audience mismatch"; return r; }
    }

    // Subject NameID (default username source).
    std::string nameId;
    if (xmlNodePtr subj = childByLocal(assertion, "Subject"))
        nameId = nodeText(childByLocal(subj, "NameID"));

    // Attributes (email + groups) from the signed assertion's AttributeStatement.
    std::string email;
    if (xmlNodePtr as = childByLocal(assertion, "AttributeStatement"))
    {
        for (xmlNodePtr at = as->children; at; at = at->next)
        {
            if (!localIs(at, "Attribute")) continue;
            const std::string name = attr(at, "Name");
            if (!m_cfg.emailAttr.empty() && name == m_cfg.emailAttr)
            {
                if (xmlNodePtr v = childByLocal(at, "AttributeValue")) email = nodeText(v);
            }
            else if (name == m_cfg.groupsAttr)
            {
                for (xmlNodePtr v = at->children; v; v = v->next)
                    if (localIs(v, "AttributeValue"))
                    {
                        const std::string g = nodeText(v);
                        if (!g.empty()) r.groups.push_back(g);
                    }
            }
        }
    }

    xmlFreeDoc(doc);

    // Group allowlist.
    if (!m_cfg.adminGroup.empty())
    {
        bool inGroup = false;
        for (const auto& g : r.groups) if (g == m_cfg.adminGroup) { inGroup = true; break; }
        if (!inGroup) { r.error = "not in admin group"; return r; }
    }

    r.username = !email.empty() ? email : nameId;
    if (r.username.empty()) { r.error = "no email/NameID"; return r; }

    r.success = true;
    return r;
}

} // namespace pz::authd
