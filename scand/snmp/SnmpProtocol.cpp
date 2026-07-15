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
#include "snmp/SnmpWalk.h"
#include "util/Logger.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

#ifndef SNMP_FLAGS_DONT_PROBE
#define SNMP_FLAGS_DONT_PROBE 0x100
#endif

namespace pz::scand
{

namespace
{

const oid kSysDescr[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
const oid kSysObjectId[] = {1, 3, 6, 1, 2, 1, 1, 2, 0};
const oid kSysUpTime[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
const oid kSysContact[] = {1, 3, 6, 1, 2, 1, 1, 4, 0};
const oid kSysName[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
const oid kSysLocation[] = {1, 3, 6, 1, 2, 1, 1, 6, 0};

constexpr size_t kSysOidLen = 9;

const oid kIfPhysAddr[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 6};
constexpr size_t kIfPhysAddrLen = 10;

const oid kIpAdEntIfIndex[] = {1, 3, 6, 1, 2, 1, 4, 20, 1, 2};
const oid kIpAdEntNetMask[] = {1, 3, 6, 1, 2, 1, 4, 20, 1, 3};
constexpr size_t kIpAdEntLen = 10;

const oid kIfDescr[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 2};
constexpr size_t kIfDescrLen = 10;

const oid kIfType[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 3};
const oid kIfOperStatus[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 8};
constexpr size_t kIfColLen = 10;

const oid kIfName[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 1};
const oid kIfHighSpeed[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15};
const oid kIfAlias[] = {1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 18};
constexpr size_t kIfXColLen = 11;

const oid kIpNetToMediaPhys[] = {1, 3, 6, 1, 2, 1, 4, 22, 1, 2};
constexpr size_t kArpColLen = 10;

std::string ipv4BytesToStr(const u_char* b)
{
    return std::to_string(b[0]) + '.' + std::to_string(b[1]) + '.' + std::to_string(b[2]) + '.' + std::to_string(b[3]);
}

std::string ipFromOidSuffix(const oid* name, size_t nameLen, size_t colLen)
{
    if (nameLen < colLen + 4)
        return {};
    return std::to_string(name[colLen]) + '.' + std::to_string(name[colLen + 1]) + '.' +
           std::to_string(name[colLen + 2]) + '.' + std::to_string(name[colLen + 3]);
}

std::string oidToStr(const oid* name, size_t len)
{
    std::string out;
    for (size_t i = 0; i < len; ++i)
    {
        if (i > 0)
            out += '.';
        out += std::to_string(static_cast<unsigned long>(name[i]));
    }
    return out;
}

}

void SnmpProtocol::configureV2c(snmp_session& sess, const std::string& peer, const std::string& community,
                                const SnmpScanConfig& cfg)
{
    sess.peername = const_cast<char*>(peer.c_str());
    sess.version = SNMP_VERSION_2c;
    sess.community = reinterpret_cast<u_char*>(const_cast<char*>(community.c_str()));
    sess.community_len = community.size();
    sess.timeout = static_cast<long>(cfg.v2cProbeTimeoutMs) * 1000L;
    sess.retries = cfg.v2cProbeRetries;
}

bool SnmpProtocol::configureV3(snmp_session& sess, const std::string& peer, const SnmpScanConfig& cfg,
                               const SnmpV3Config& v3)
{
    if (v3.user.empty())
    {
        LOG_WARN("v3 user is empty, cannot configure v3");
        return false;
    }

    sess.peername = const_cast<char*>(peer.c_str());
    sess.version = SNMP_VERSION_3;
    sess.timeout = static_cast<long>(cfg.timeoutMs) * 1000L;
    sess.retries = cfg.retries;

    sess.flags |= SNMP_FLAGS_DONT_PROBE;

    sess.securityName = const_cast<char*>(v3.user.c_str());
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

    if (v3.level != SnmpSecurityLevel::NoAuthNoPriv)
    {
        if (v3.authProtocol == "MD5")
        {
            sess.securityAuthProto = usmHMACMD5AuthProtocol;
            sess.securityAuthProtoLen = OID_LENGTH(usmHMACMD5AuthProtocol);
        }
        else
        {
            sess.securityAuthProto = usmHMACSHA1AuthProtocol;
            sess.securityAuthProtoLen = OID_LENGTH(usmHMACSHA1AuthProtocol);
        }

        sess.securityAuthKeyLen = USM_AUTH_KU_LEN;
        if (generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                        reinterpret_cast<const u_char*>(v3.authPassword.c_str()), v3.authPassword.size(),
                        sess.securityAuthKey, &sess.securityAuthKeyLen) != SNMPERR_SUCCESS)
        {
            LOG_WARN("auth key generation failed (user={})", v3.user);
            return false;
        }
    }

    if (v3.level == SnmpSecurityLevel::AuthPriv)
    {
        if (v3.privProtocol == "DES")
        {
            sess.securityPrivProto = usmDESPrivProtocol;
            sess.securityPrivProtoLen = OID_LENGTH(usmDESPrivProtocol);
        }
        else
        {
            sess.securityPrivProto = usmAESPrivProtocol;
            sess.securityPrivProtoLen = OID_LENGTH(usmAESPrivProtocol);
        }

        sess.securityPrivKeyLen = USM_PRIV_KU_LEN;
        if (generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                        reinterpret_cast<const u_char*>(v3.privPassword.c_str()), v3.privPassword.size(),
                        sess.securityPrivKey, &sess.securityPrivKeyLen) != SNMPERR_SUCCESS)
        {
            LOG_WARN("priv key generation failed (user={})", v3.user);
            return false;
        }
    }

    return true;
}

snmp_pdu* SnmpProtocol::buildSysGroupGet()
{
    netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GET);
    snmp_add_null_var(pdu, kSysDescr, kSysOidLen);
    snmp_add_null_var(pdu, kSysObjectId, kSysOidLen);
    snmp_add_null_var(pdu, kSysUpTime, kSysOidLen);
    snmp_add_null_var(pdu, kSysContact, kSysOidLen);
    snmp_add_null_var(pdu, kSysName, kSysOidLen);
    snmp_add_null_var(pdu, kSysLocation, kSysOidLen);
    return pdu;
}

void SnmpProtocol::parseSysGroup(snmp_pdu* pdu, SnmpDevice& dev)
{
    for (netsnmp_variable_list* var = pdu->variables; var != nullptr; var = var->next_variable)
    {
        if (var->type == ASN_OCTET_STR && var->val.string != nullptr)
        {
            const std::string val(reinterpret_cast<const char*>(var->val.string), var->val_len);

            if (snmp_oid_compare(var->name, var->name_length, kSysDescr, kSysOidLen) == 0)
                dev.sysDescr = val;
            else if (snmp_oid_compare(var->name, var->name_length, kSysContact, kSysOidLen) == 0)
                dev.sysContact = val;
            else if (snmp_oid_compare(var->name, var->name_length, kSysName, kSysOidLen) == 0)
                dev.sysName = val;
            else if (snmp_oid_compare(var->name, var->name_length, kSysLocation, kSysOidLen) == 0)
                dev.sysLocation = val;
        }
        else if (var->type == ASN_TIMETICKS && var->val.integer != nullptr)
        {
            if (snmp_oid_compare(var->name, var->name_length, kSysUpTime, kSysOidLen) == 0)
                dev.sysUpTimeTicks = static_cast<uint32_t>(*var->val.integer);
        }
        else if (var->type == ASN_OBJECT_ID && var->val.objid != nullptr)
        {
            if (snmp_oid_compare(var->name, var->name_length, kSysObjectId, kSysOidLen) == 0)
                dev.sysObjectId = oidToStr(var->val.objid, var->val_len / sizeof(oid));
        }
    }
}

void SnmpProtocol::walkIfPhysAddr(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    oid cur[MAX_OID_LEN];
    size_t curLen = kIfPhysAddrLen;
    std::memcpy(cur, kIfPhysAddr, kIfPhysAddrLen * sizeof(oid));

    for (int guard = 0; guard < 1024; ++guard)
    {
        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        snmp_add_null_var(pdu, cur, curLen);

        netsnmp_pdu* resp = nullptr;
        const int status = snmp_sess_synch_response(sessp, pdu, &resp);

        if (status != STAT_SUCCESS || resp == nullptr || resp->errstat != SNMP_ERR_NOERROR ||
            resp->variables == nullptr)
        {
            if (resp)
                snmp_free_pdu(resp);
            break;
        }

        netsnmp_variable_list* var = resp->variables;

        bool inSubtree = var->name_length >= kIfPhysAddrLen;
        for (size_t i = 0; inSubtree && i < kIfPhysAddrLen; ++i)
            if (var->name[i] != kIfPhysAddr[i])
                inSubtree = false;

        if (!inSubtree || var->type == SNMP_ENDOFMIBVIEW || var->type == SNMP_NOSUCHOBJECT ||
            var->type == SNMP_NOSUCHINSTANCE)
        {
            snmp_free_pdu(resp);
            break;
        }

        if (var->type == ASN_OCTET_STR && var->val.string != nullptr && var->val_len == 6)
        {
            const bool allZero = var->val.string[0] == 0 && var->val.string[1] == 0 && var->val.string[2] == 0 &&
                                 var->val.string[3] == 0 && var->val.string[4] == 0 && var->val.string[5] == 0;
            if (!allZero)
            {
                std::string mac = macToStr(var->val.string, var->val_len);
                if (std::find(dev.interfaceMacs.begin(), dev.interfaceMacs.end(), mac) == dev.interfaceMacs.end())
                    dev.interfaceMacs.push_back(std::move(mac));
            }
        }

        std::memcpy(cur, var->name, var->name_length * sizeof(oid));
        curLen = var->name_length;
        snmp_free_pdu(resp);
    }
}

void SnmpProtocol::walkInterfaceAddrs(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    std::map<std::string, SnmpInterface> byIp;

    walkColumn(sessp, kIpAdEntIfIndex, kIpAdEntLen,
               [&](netsnmp_variable_list* var)
               {
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

    walkColumn(sessp, kIpAdEntNetMask, kIpAdEntLen,
               [&](netsnmp_variable_list* var)
               {
                   const std::string ip = ipFromOidSuffix(var->name, var->name_length, kIpAdEntLen);
                   auto it = byIp.find(ip);
                   if (it == byIp.end())
                       return;
                   if (var->type == ASN_IPADDRESS && var->val.string != nullptr && var->val_len == 4)
                       it->second.netmask = ipv4BytesToStr(var->val.string);
               });

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
        if (status == STAT_SUCCESS && resp != nullptr && resp->errstat == SNMP_ERR_NOERROR &&
            resp->variables != nullptr && resp->variables->type == ASN_OCTET_STR &&
            resp->variables->val.string != nullptr)
        {
            ifName.assign(reinterpret_cast<const char*>(resp->variables->val.string), resp->variables->val_len);
        }
        if (resp)
            snmp_free_pdu(resp);
        nameByIdx[iface.ifIndex] = ifName;
    }

    for (auto& [ip, iface] : byIp)
    {
        auto it = nameByIdx.find(iface.ifIndex);
        if (it != nameByIdx.end())
            iface.ifName = it->second;
        dev.interfaces.push_back(std::move(iface));
    }

    LOG_TRACE("walked interfaces (ip={}, interfaces={})", dev.ip, dev.interfaces.size());
}

void SnmpProtocol::walkIfTable(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    std::map<uint32_t, SnmpIfEntry> byIdx;
    auto idxOf = [](const netsnmp_variable_list* var, size_t colLen) -> uint32_t
    { return var->name_length > colLen ? static_cast<uint32_t>(var->name[colLen]) : 0; };

    walkColumn(sessp, kIfDescr, kIfColLen,
               [&](netsnmp_variable_list* var)
               {
                   auto& e = byIdx[idxOf(var, kIfColLen)];
                   e.descr = octetDisplay(var);
               });
    walkColumn(sessp, kIfType, kIfColLen,
               [&](netsnmp_variable_list* var) { byIdx[idxOf(var, kIfColLen)].type = static_cast<int>(varInt(var)); });
    walkColumn(sessp, kIfOperStatus, kIfColLen,
               [&](netsnmp_variable_list* var)
               { byIdx[idxOf(var, kIfColLen)].operStatus = static_cast<int>(varInt(var)); });
    walkColumn(sessp, kIfPhysAddr, kIfColLen,
               [&](netsnmp_variable_list* var)
               {
                   if (var->type == ASN_OCTET_STR && var->val.string != nullptr && var->val_len == 6)
                       byIdx[idxOf(var, kIfColLen)].mac = macToStr(var->val.string, var->val_len);
               });
    walkColumn(sessp, kIfName, kIfXColLen,
               [&](netsnmp_variable_list* var) { byIdx[idxOf(var, kIfXColLen)].name = octetDisplay(var); });
    walkColumn(sessp, kIfAlias, kIfXColLen,
               [&](netsnmp_variable_list* var) { byIdx[idxOf(var, kIfXColLen)].alias = octetDisplay(var); });
    walkColumn(sessp, kIfHighSpeed, kIfXColLen,
               [&](netsnmp_variable_list* var)
               { byIdx[idxOf(var, kIfXColLen)].speed = static_cast<uint64_t>(varInt(var)); });

    for (auto& [idx, e] : byIdx)
    {
        e.ifIndex = idx;
        if (e.name.empty())
            e.name = e.descr;
        dev.ifTable.push_back(std::move(e));
    }
    LOG_TRACE("walked ifTable (ip={}, if_table={})", dev.ip, dev.ifTable.size());
}

void SnmpProtocol::walkArpTable(void* sessp, SnmpDevice& dev)
{
    if (sessp == nullptr)
        return;

    walkColumn(sessp, kIpNetToMediaPhys, kArpColLen,
               [&](netsnmp_variable_list* var)
               {
                   if (var->type != ASN_OCTET_STR || var->val.string == nullptr || var->val_len != 6)
                       return;
                   if (var->name_length < kArpColLen + 5)
                       return;
                   SnmpArpEntry e;
                   e.ifIndex = static_cast<uint32_t>(var->name[kArpColLen]);
                   e.ip = ipFromOidSuffix(var->name, var->name_length, kArpColLen + 1);
                   e.mac = macToStr(var->val.string, var->val_len);
                   if (!e.ip.empty())
                       dev.arpEntries.push_back(std::move(e));
               });
    LOG_TRACE("walked arpTable (ip={}, arp_entries={})", dev.ip, dev.arpEntries.size());
}

}
