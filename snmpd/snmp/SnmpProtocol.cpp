// net-snmp must come before our Logger: it pulls in syslog.h which defines
// LOG_INFO / LOG_DEBUG / etc. as integers — undef them afterwards.
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#undef LOG_EMERG
#undef LOG_ALERT
#undef LOG_CRIT
#undef LOG_ERR
#undef LOG_WARNING
#undef LOG_NOTICE
#undef LOG_INFO
#undef LOG_DEBUG

#include "snmp/SnmpProtocol.h"
#include "util/Logger.h"

// Session flag to skip the SYNCHRONOUS engineID probe that snmp_sess_open()
// otherwise performs for v3. With it set, engineID discovery happens
// asynchronously via the normal request/REPORT cycle, so a dead host times out
// in our poll loop instead of blocking snmp_sess_open() for the full v3 timeout
// (which serialised every v3 fallback). Value is stable across net-snmp 5.x;
// the macro itself is not exported by 5.9.1's public headers.
#ifndef SNMP_FLAGS_DONT_PROBE
#define SNMP_FLAGS_DONT_PROBE 0x100
#endif

namespace pz::snmpd
{

namespace
{

// sysGroup OIDs (RFC 1213, 1.3.6.1.2.1.1.N.0)
const oid kSysDescr[]    = {1,3,6,1,2,1,1,1,0};
const oid kSysObjectId[] = {1,3,6,1,2,1,1,2,0};
const oid kSysUpTime[]   = {1,3,6,1,2,1,1,3,0};
const oid kSysContact[]  = {1,3,6,1,2,1,1,4,0};
const oid kSysName[]     = {1,3,6,1,2,1,1,5,0};
const oid kSysLocation[] = {1,3,6,1,2,1,1,6,0};

constexpr size_t kSysOidLen = 9;

std::string oidToStr(const oid* name, size_t len)
{
    std::string out;
    for (size_t i = 0; i < len; ++i)
    {
        if (i > 0) out += '.';
        out += std::to_string(static_cast<unsigned long>(name[i]));
    }
    return out;
}

} // namespace

void SnmpProtocol::configureV2c(snmp_session& sess,
                                const std::string& peer,
                                const SnmpScanConfig& cfg)
{
    sess.peername      = const_cast<char*>(peer.c_str());
    sess.version       = SNMP_VERSION_2c;
    sess.community     = reinterpret_cast<u_char*>(
                             const_cast<char*>(cfg.community.c_str()));
    sess.community_len = cfg.community.size();
    // Short probe timeout — we only need to learn whether v2c works; a v3-only
    // host falls back fast instead of waiting the full v3 fetch timeout.
    sess.timeout       = static_cast<long>(cfg.v2cProbeTimeoutMs) * 1000L;
    sess.retries       = cfg.v2cProbeRetries;
}

bool SnmpProtocol::configureV3(snmp_session& sess,
                               const std::string& peer,
                               const SnmpScanConfig& cfg,
                               const SnmpV3Config& v3)
{
    if (v3.user.empty())
    {
        LOG_WARN("SnmpProtocol: v3 user is empty, cannot configure v3");
        return false;
    }

    sess.peername = const_cast<char*>(peer.c_str());
    sess.version  = SNMP_VERSION_3;
    sess.timeout  = static_cast<long>(cfg.timeoutMs) * 1000L;
    sess.retries  = cfg.retries;

    // Async engineID discovery — never block snmp_sess_open() on a probe.
    sess.flags |= SNMP_FLAGS_DONT_PROBE;

    sess.securityName    = const_cast<char*>(v3.user.c_str());
    sess.securityNameLen = v3.user.size();

    switch (v3.level)
    {
    case SnmpSecurityLevel::NoAuthNoPriv:
        sess.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
        break;
    case SnmpSecurityLevel::AuthNoPriv:
        sess.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;
        break;
    case SnmpSecurityLevel::AuthPriv:
    default:
        sess.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;
        break;
    }

    // ── Authentication key ───────────────────────────────────────────────────
    if (v3.level != SnmpSecurityLevel::NoAuthNoPriv)
    {
        if (v3.authProtocol == "MD5")
        {
            sess.securityAuthProto    = usmHMACMD5AuthProtocol;
            sess.securityAuthProtoLen = OID_LENGTH(usmHMACMD5AuthProtocol);
        }
        else // default SHA
        {
            sess.securityAuthProto    = usmHMACSHA1AuthProtocol;
            sess.securityAuthProtoLen = OID_LENGTH(usmHMACSHA1AuthProtocol);
        }

        sess.securityAuthKeyLen = USM_AUTH_KU_LEN;
        if (generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                        reinterpret_cast<const u_char*>(v3.authPassword.c_str()),
                        v3.authPassword.size(),
                        sess.securityAuthKey, &sess.securityAuthKeyLen)
                != SNMPERR_SUCCESS)
        {
            LOG_WARN("SnmpProtocol: auth key generation failed user={}", v3.user);
            return false;
        }
    }

    // ── Privacy key (derived with the auth hash, per RFC 3414) ───────────────
    if (v3.level == SnmpSecurityLevel::AuthPriv)
    {
        if (v3.privProtocol == "DES")
        {
            sess.securityPrivProto    = usmDESPrivProtocol;
            sess.securityPrivProtoLen = OID_LENGTH(usmDESPrivProtocol);
        }
        else // default AES
        {
            sess.securityPrivProto    = usmAESPrivProtocol;
            sess.securityPrivProtoLen = OID_LENGTH(usmAESPrivProtocol);
        }

        sess.securityPrivKeyLen = USM_PRIV_KU_LEN;
        if (generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                        reinterpret_cast<const u_char*>(v3.privPassword.c_str()),
                        v3.privPassword.size(),
                        sess.securityPrivKey, &sess.securityPrivKeyLen)
                != SNMPERR_SUCCESS)
        {
            LOG_WARN("SnmpProtocol: priv key generation failed user={}", v3.user);
            return false;
        }
    }

    return true;
}

snmp_pdu* SnmpProtocol::buildSysGroupGet()
{
    netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GET);
    snmp_add_null_var(pdu, kSysDescr,    kSysOidLen);
    snmp_add_null_var(pdu, kSysObjectId, kSysOidLen);
    snmp_add_null_var(pdu, kSysUpTime,   kSysOidLen);
    snmp_add_null_var(pdu, kSysContact,  kSysOidLen);
    snmp_add_null_var(pdu, kSysName,     kSysOidLen);
    snmp_add_null_var(pdu, kSysLocation, kSysOidLen);
    return pdu;
}

void SnmpProtocol::parseSysGroup(snmp_pdu* pdu, SnmpDevice& dev)
{
    for (netsnmp_variable_list* var = pdu->variables;
         var != nullptr; var = var->next_variable)
    {
        if (var->type == ASN_OCTET_STR && var->val.string != nullptr)
        {
            const std::string val(
                reinterpret_cast<const char*>(var->val.string),
                var->val_len);

            if (snmp_oid_compare(var->name, var->name_length,
                                 kSysDescr, kSysOidLen) == 0)
                dev.sysDescr = val;
            else if (snmp_oid_compare(var->name, var->name_length,
                                      kSysContact, kSysOidLen) == 0)
                dev.sysContact = val;
            else if (snmp_oid_compare(var->name, var->name_length,
                                      kSysName, kSysOidLen) == 0)
                dev.sysName = val;
            else if (snmp_oid_compare(var->name, var->name_length,
                                      kSysLocation, kSysOidLen) == 0)
                dev.sysLocation = val;
        }
        else if (var->type == ASN_TIMETICKS && var->val.integer != nullptr)
        {
            if (snmp_oid_compare(var->name, var->name_length,
                                 kSysUpTime, kSysOidLen) == 0)
                dev.sysUpTimeTicks = static_cast<uint32_t>(*var->val.integer);
        }
        else if (var->type == ASN_OBJECT_ID && var->val.objid != nullptr)
        {
            if (snmp_oid_compare(var->name, var->name_length,
                                 kSysObjectId, kSysOidLen) == 0)
                dev.sysObjectId = oidToStr(var->val.objid,
                                           var->val_len / sizeof(oid));
        }
    }
}

} // namespace pz::snmpd
