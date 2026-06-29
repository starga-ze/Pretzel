#pragma once

// Shared net-snmp walk helpers used by both the sysGroup/ifTable/ARP walkers in
// SnmpProtocol.cpp and the LLDP walker in lldp/LldpProtocol.cpp. Header-only so it
// has no link-time footprint; relies on net-snmp types (oid, netsnmp_pdu, ...), so
// it must be included AFTER <net-snmp/net-snmp-includes.h> in the including .cpp.

#include <cstddef>
#include <cstring>
#include <string>

namespace pz::scand
{

// Render an octet-string varbind: printable ASCII as text, otherwise hex (colon-
// separated), which suits MAC-style chassis/port IDs.
inline std::string octetDisplay(const netsnmp_variable_list* var)
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

    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(n * 3);
    for (size_t i = 0; i < n; ++i)
    {
        if (i) out += ':';
        out += hexd[(s[i] >> 4) & 0xF];
        out += hexd[s[i] & 0xF];
    }
    return out;
}

inline std::string macToStr(const u_char* b, size_t len)
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

// Read an integer-typed varbind, or 0.
inline long varInt(const netsnmp_variable_list* var)
{
    return (var->val.integer != nullptr) ? *var->val.integer : 0;
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

} // namespace pz::scand
