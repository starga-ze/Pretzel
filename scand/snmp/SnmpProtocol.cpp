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

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

// Session flag to skip the SYNCHRONOUS engineID probe that snmp_sess_open()
// otherwise performs for v3. With it set, engineID discovery happens
// asynchronously via the normal request/REPORT cycle, so a dead host times out
// in our poll loop instead of blocking snmp_sess_open() for the full v3 timeout
// (which serialised every v3 fallback). Value is stable across net-snmp 5.x;
// the macro itself is not exported by 5.9.1's public headers.
#ifndef SNMP_FLAGS_DONT_PROBE
#define SNMP_FLAGS_DONT_PROBE 0x100
#endif

namespace pz::scand
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

// ifPhysAddress column (IF-MIB ifTable col 6): 1.3.6.1.2.1.2.2.1.6
const oid kIfPhysAddr[] = {1,3,6,1,2,1,2,2,1,6};
constexpr size_t kIfPhysAddrLen = 10;

// ip-MIB ipAddrTable columns (1.3.6.1.2.1.4.20.1.N), indexed by the IPv4 address.
const oid kIpAdEntIfIndex[] = {1,3,6,1,2,1,4,20,1,2};
const oid kIpAdEntNetMask[] = {1,3,6,1,2,1,4,20,1,3};
constexpr size_t kIpAdEntLen = 10;

// ifDescr column (IF-MIB ifTable col 2): 1.3.6.1.2.1.2.2.1.2.<ifIndex>
const oid kIfDescr[] = {1,3,6,1,2,1,2,2,1,2};
constexpr size_t kIfDescrLen = 10;

// IF-MIB ifTable columns (1.3.6.1.2.1.2.2.1.N), indexed by ifIndex.
const oid kIfType[]       = {1,3,6,1,2,1,2,2,1,3};
const oid kIfOperStatus[] = {1,3,6,1,2,1,2,2,1,8};
constexpr size_t kIfColLen = 10;

// IF-MIB ifXTable columns (1.3.6.1.2.1.31.1.1.1.N), indexed by ifIndex.
const oid kIfName[]      = {1,3,6,1,2,1,31,1,1,1,1};
const oid kIfHighSpeed[] = {1,3,6,1,2,1,31,1,1,1,15};
const oid kIfAlias[]     = {1,3,6,1,2,1,31,1,1,1,18};
constexpr size_t kIfXColLen = 11;

// LLDP-MIB lldpRemTable columns (1.0.8802.1.1.2.1.4.1.1.N); index =
// {timeMark, lldpRemLocalPortNum, lldpRemIndex}.
const oid kLldpRemChassisId[] = {1,0,8802,1,1,2,1,4,1,1,5};
const oid kLldpRemPortId[]    = {1,0,8802,1,1,2,1,4,1,1,7};
const oid kLldpRemSysName[]   = {1,0,8802,1,1,2,1,4,1,1,9};
const oid kLldpRemSysDesc[]   = {1,0,8802,1,1,2,1,4,1,1,10};
constexpr size_t kLldpRemColLen = 11;

// LLDP-MIB lldpLocPortTable lldpLocPortDesc (1.0.8802.1.1.2.1.3.7.1.4); index =
// lldpLocPortNum.
const oid kLldpLocPortDesc[] = {1,0,8802,1,1,2,1,3,7,1,4};
constexpr size_t kLldpLocColLen = 11;

// ip-MIB ipNetToMediaPhysAddress (ARP): 1.3.6.1.2.1.4.22.1.2; index = {ifIndex, ip}.
const oid kIpNetToMediaPhys[] = {1,3,6,1,2,1,4,22,1,2};
constexpr size_t kArpColLen = 10;

std::string macToStr(const u_char* b, size_t len);  // defined below

// Read an integer-typed varbind, or 0.
long varInt(const netsnmp_variable_list* var)
{
    return (var->val.integer != nullptr) ? *var->val.integer : 0;
}

// Render an octet-string varbind: printable ASCII as text, otherwise hex (colon-
// separated), which suits MAC-style chassis/port IDs.
std::string octetDisplay(const netsnmp_variable_list* var)
{
    if (var->type != ASN_OCTET_STR || var->val.string == nullptr)
        return {};
    const u_char* s = var->val.string;
    const size_t  n = var->val_len;
    bool printable = n > 0;
    for (size_t i = 0; i < n; ++i)
        if (s[i] < 0x20 || s[i] > 0x7e) { printable = false; break; }
    if (printable)
        return std::string(reinterpret_cast<const char*>(s), n);
    return macToStr(s, n);
}

// Render a 4-byte IPv4 (network order) as dotted decimal.
std::string ipv4BytesToStr(const u_char* b)
{
    return std::to_string(b[0]) + '.' + std::to_string(b[1]) + '.' +
           std::to_string(b[2]) + '.' + std::to_string(b[3]);
}

// The ipAddrTable index is the IPv4 address: the 4 OID sub-ids after the column.
std::string ipFromOidSuffix(const oid* name, size_t nameLen, size_t colLen)
{
    if (nameLen < colLen + 4)
        return {};
    return std::to_string(name[colLen])     + '.' + std::to_string(name[colLen + 1]) + '.' +
           std::to_string(name[colLen + 2]) + '.' + std::to_string(name[colLen + 3]);
}

// GETNEXT-walk a single MIB column on an open session handle, invoking `onVar` for
// each in-subtree varbind. Bounded against runaway agents.
template <class F>
void walkColumn(void* sessp, const oid* base, size_t baseLen, F&& onVar)
{
    oid    cur[MAX_OID_LEN];
    size_t curLen = baseLen;
    std::memcpy(cur, base, baseLen * sizeof(oid));

    for (int guard = 0; guard < 4096; ++guard)
    {
        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        snmp_add_null_var(pdu, cur, curLen);

        netsnmp_pdu* resp = nullptr;
        const int status = snmp_sess_synch_response(sessp, pdu, &resp);

        if (status != STAT_SUCCESS || resp == nullptr ||
            resp->errstat != SNMP_ERR_NOERROR || resp->variables == nullptr)
        {
            if (resp) snmp_free_pdu(resp);
            break;
        }

        netsnmp_variable_list* var = resp->variables;

        bool inSubtree = var->name_length >= baseLen;
        for (size_t i = 0; inSubtree && i < baseLen; ++i)
            if (var->name[i] != base[i]) inSubtree = false;

        if (!inSubtree ||
            var->type == SNMP_ENDOFMIBVIEW ||
            var->type == SNMP_NOSUCHOBJECT ||
            var->type == SNMP_NOSUCHINSTANCE)
        {
            snmp_free_pdu(resp);
            break;
        }

        onVar(var);

        std::memcpy(cur, var->name, var->name_length * sizeof(oid));
        curLen = var->name_length;
        snmp_free_pdu(resp);
    }
}

std::string macToStr(const u_char* b, size_t len)
{
    static const char* hexd = "0123456789ABCDEF";
    std::string s;
    s.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
    {
        if (i) s += ':';
        s += hexd[(b[i] >> 4) & 0xF];
        s += hexd[b[i] & 0xF];
    }
    return s;
}

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

void SnmpProtocol::walkIfPhysAddr(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    oid    cur[MAX_OID_LEN];
    size_t curLen = kIfPhysAddrLen;
    std::memcpy(cur, kIfPhysAddr, kIfPhysAddrLen * sizeof(oid));

    // GETNEXT walk of the ifPhysAddress column. Guard against runaway agents.
    for (int guard = 0; guard < 1024; ++guard)
    {
        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        snmp_add_null_var(pdu, cur, curLen);

        netsnmp_pdu* resp = nullptr;
        const int status = snmp_sess_synch_response(sessp, pdu, &resp);

        if (status != STAT_SUCCESS || resp == nullptr ||
            resp->errstat != SNMP_ERR_NOERROR || resp->variables == nullptr)
        {
            if (resp) snmp_free_pdu(resp);
            break;
        }

        netsnmp_variable_list* var = resp->variables;

        // Left the ifPhysAddress subtree, or end-of-MIB → done.
        bool inSubtree = var->name_length >= kIfPhysAddrLen;
        for (size_t i = 0; inSubtree && i < kIfPhysAddrLen; ++i)
            if (var->name[i] != kIfPhysAddr[i]) inSubtree = false;

        if (!inSubtree ||
            var->type == SNMP_ENDOFMIBVIEW ||
            var->type == SNMP_NOSUCHOBJECT ||
            var->type == SNMP_NOSUCHINSTANCE)
        {
            snmp_free_pdu(resp);
            break;
        }

        // A populated physical interface reports a 6-byte MAC; logical/HA ifaces
        // return an empty octet string or an all-zero MAC — skip those, and dedupe
        // (agents repeat the chassis MAC across every ifTable row), since the MAC set
        // is the device-grouping fingerprint.
        if (var->type == ASN_OCTET_STR && var->val.string != nullptr && var->val_len == 6)
        {
            const bool allZero =
                var->val.string[0] == 0 && var->val.string[1] == 0 && var->val.string[2] == 0 &&
                var->val.string[3] == 0 && var->val.string[4] == 0 && var->val.string[5] == 0;
            if (!allZero)
            {
                std::string mac = macToStr(var->val.string, var->val_len);
                if (std::find(dev.interfaceMacs.begin(), dev.interfaceMacs.end(), mac) ==
                    dev.interfaceMacs.end())
                    dev.interfaceMacs.push_back(std::move(mac));
            }
        }

        // Advance the cursor to the OID we just received.
        std::memcpy(cur, var->name, var->name_length * sizeof(oid));
        curLen = var->name_length;
        snmp_free_pdu(resp);
    }
}

void SnmpProtocol::walkInterfaceAddrs(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    // ip -> {ifIndex, netmask}, keyed by the ipAddrTable index (the IP itself).
    std::map<std::string, SnmpInterface> byIp;

    // ipAdEntIfIndex: index = IP (OID suffix), value = ifIndex (INTEGER).
    walkColumn(sessp, kIpAdEntIfIndex, kIpAdEntLen, [&](netsnmp_variable_list* var) {
        const std::string ip = ipFromOidSuffix(var->name, var->name_length, kIpAdEntLen);
        if (ip.empty() || ip == "127.0.0.1")
            return;
        auto& iface = byIp[ip];
        iface.ip = ip;
        if (var->type == ASN_INTEGER && var->val.integer != nullptr)
            iface.ifIndex = static_cast<uint32_t>(*var->val.integer);
    });

    if (byIp.empty())
        return;

    // ipAdEntNetMask: index = IP, value = mask (IpAddress, 4 bytes).
    walkColumn(sessp, kIpAdEntNetMask, kIpAdEntLen, [&](netsnmp_variable_list* var) {
        const std::string ip = ipFromOidSuffix(var->name, var->name_length, kIpAdEntLen);
        auto it = byIp.find(ip);
        if (it == byIp.end())
            return;
        if (var->type == ASN_IPADDRESS && var->val.string != nullptr && var->val_len == 4)
            it->second.netmask = ipv4BytesToStr(var->val.string);
    });

    // Resolve interface names: targeted GET of ifDescr.<ifIndex> per unique ifIndex.
    std::map<uint32_t, std::string> nameByIdx;
    for (const auto& [ip, iface] : byIp)
    {
        if (iface.ifIndex == 0 || nameByIdx.count(iface.ifIndex))
            continue;

        oid name[MAX_OID_LEN];
        std::memcpy(name, kIfDescr, kIfDescrLen * sizeof(oid));
        name[kIfDescrLen] = iface.ifIndex;
        const size_t nameLen = kIfDescrLen + 1;

        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GET);
        snmp_add_null_var(pdu, name, nameLen);

        netsnmp_pdu* resp = nullptr;
        const int status = snmp_sess_synch_response(sessp, pdu, &resp);

        std::string ifName;
        if (status == STAT_SUCCESS && resp != nullptr &&
            resp->errstat == SNMP_ERR_NOERROR && resp->variables != nullptr &&
            resp->variables->type == ASN_OCTET_STR && resp->variables->val.string != nullptr)
        {
            ifName.assign(reinterpret_cast<const char*>(resp->variables->val.string),
                          resp->variables->val_len);
        }
        if (resp) snmp_free_pdu(resp);
        nameByIdx[iface.ifIndex] = ifName;
    }

    for (auto& [ip, iface] : byIp)
    {
        auto it = nameByIdx.find(iface.ifIndex);
        if (it != nameByIdx.end())
            iface.ifName = it->second;
        dev.interfaces.push_back(std::move(iface));
    }

    LOG_DEBUG("SnmpProtocol: ip={} interfaces={}", dev.ip, dev.interfaces.size());
}

void SnmpProtocol::walkIfTable(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    std::map<uint32_t, SnmpIfEntry> byIdx;
    auto idxOf = [](const netsnmp_variable_list* var, size_t colLen) -> uint32_t {
        return var->name_length > colLen ? static_cast<uint32_t>(var->name[colLen]) : 0;
    };

    walkColumn(sessp, kIfDescr, kIfColLen, [&](netsnmp_variable_list* var) {
        auto& e = byIdx[idxOf(var, kIfColLen)]; e.descr = octetDisplay(var);
    });
    walkColumn(sessp, kIfType, kIfColLen, [&](netsnmp_variable_list* var) {
        byIdx[idxOf(var, kIfColLen)].type = static_cast<int>(varInt(var));
    });
    walkColumn(sessp, kIfOperStatus, kIfColLen, [&](netsnmp_variable_list* var) {
        byIdx[idxOf(var, kIfColLen)].operStatus = static_cast<int>(varInt(var));
    });
    walkColumn(sessp, kIfPhysAddr, kIfColLen, [&](netsnmp_variable_list* var) {
        if (var->type == ASN_OCTET_STR && var->val.string != nullptr && var->val_len == 6)
            byIdx[idxOf(var, kIfColLen)].mac = macToStr(var->val.string, var->val_len);
    });
    walkColumn(sessp, kIfName, kIfXColLen, [&](netsnmp_variable_list* var) {
        byIdx[idxOf(var, kIfXColLen)].name = octetDisplay(var);
    });
    walkColumn(sessp, kIfAlias, kIfXColLen, [&](netsnmp_variable_list* var) {
        byIdx[idxOf(var, kIfXColLen)].alias = octetDisplay(var);
    });
    walkColumn(sessp, kIfHighSpeed, kIfXColLen, [&](netsnmp_variable_list* var) {
        byIdx[idxOf(var, kIfXColLen)].speed = static_cast<uint64_t>(varInt(var));
    });

    for (auto& [idx, e] : byIdx)
    {
        e.ifIndex = idx;
        if (e.name.empty()) e.name = e.descr;     // fall back to ifDescr
        dev.ifTable.push_back(std::move(e));
    }
    LOG_DEBUG("SnmpProtocol: ip={} ifTable={}", dev.ip, dev.ifTable.size());
}

void SnmpProtocol::walkLldpNeighbors(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    // Local port names: lldpLocPortNum -> port desc.
    std::map<uint32_t, std::string> locPortName;
    walkColumn(sessp, kLldpLocPortDesc, kLldpLocColLen, [&](netsnmp_variable_list* var) {
        if (var->name_length > kLldpLocColLen)
            locPortName[static_cast<uint32_t>(var->name[kLldpLocColLen])] = octetDisplay(var);
    });

    // Remote table, keyed by (localPortNum, remIndex).
    std::map<std::pair<uint32_t, uint32_t>, SnmpLldpNeighbor> byKey;
    auto keyOf = [](const netsnmp_variable_list* var) -> std::pair<uint32_t, uint32_t> {
        // index after column = {timeMark, localPortNum, remIndex}
        if (var->name_length < kLldpRemColLen + 3) return {0, 0};
        return {static_cast<uint32_t>(var->name[kLldpRemColLen + 1]),
                static_cast<uint32_t>(var->name[kLldpRemColLen + 2])};
    };

    walkColumn(sessp, kLldpRemSysName, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        byKey[keyOf(var)].remoteSysName = octetDisplay(var);
    });
    walkColumn(sessp, kLldpRemSysDesc, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        byKey[keyOf(var)].remoteSysDescr = octetDisplay(var);
    });
    walkColumn(sessp, kLldpRemPortId, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        byKey[keyOf(var)].remotePortId = octetDisplay(var);
    });
    walkColumn(sessp, kLldpRemChassisId, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        byKey[keyOf(var)].remoteChassisId = octetDisplay(var);
    });

    for (auto& [key, n] : byKey)
    {
        n.localPort = key.first;
        auto it = locPortName.find(key.first);
        if (it != locPortName.end())
            n.localPortName = it->second;
        dev.lldpNeighbors.push_back(std::move(n));
    }
    LOG_DEBUG("SnmpProtocol: ip={} lldpNeighbors={}", dev.ip, dev.lldpNeighbors.size());
}

void SnmpProtocol::walkArpTable(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    walkColumn(sessp, kIpNetToMediaPhys, kArpColLen, [&](netsnmp_variable_list* var) {
        if (var->type != ASN_OCTET_STR || var->val.string == nullptr || var->val_len != 6)
            return;
        // index = {ifIndex, ip(4)}
        if (var->name_length < kArpColLen + 5)
            return;
        SnmpArpEntry e;
        e.ifIndex = static_cast<uint32_t>(var->name[kArpColLen]);
        e.ip      = ipFromOidSuffix(var->name, var->name_length, kArpColLen + 1);
        e.mac     = macToStr(var->val.string, var->val_len);
        if (!e.ip.empty())
            dev.arpEntries.push_back(std::move(e));
    });
    LOG_DEBUG("SnmpProtocol: ip={} arpEntries={}", dev.ip, dev.arpEntries.size());
}

} // namespace pz::scand
