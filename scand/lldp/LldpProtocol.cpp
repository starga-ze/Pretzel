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

#include "lldp/LldpProtocol.h"
#include "snmp/SnmpWalk.h"
#include "util/Logger.h"

#include <map>
#include <utility>

namespace pz::scand
{

namespace
{

// LLDP-MIB lldpRemTable columns (1.0.8802.1.1.2.1.4.1.1.N); index =
// {timeMark, lldpRemLocalPortNum, lldpRemIndex}.
const oid kLldpRemChassisIdSubtype[] = {1,0,8802,1,1,2,1,4,1,1,4};
const oid kLldpRemChassisId[]        = {1,0,8802,1,1,2,1,4,1,1,5};
const oid kLldpRemPortId[]           = {1,0,8802,1,1,2,1,4,1,1,7};
const oid kLldpRemSysName[]          = {1,0,8802,1,1,2,1,4,1,1,9};
const oid kLldpRemSysDesc[]          = {1,0,8802,1,1,2,1,4,1,1,10};
const oid kLldpRemSysCapEnabled[]    = {1,0,8802,1,1,2,1,4,1,1,12};
constexpr size_t kLldpRemColLen = 11;

// LLDP-MIB lldpLocPortTable lldpLocPortDesc (1.0.8802.1.1.2.1.3.7.1.4); index =
// lldpLocPortNum.
const oid kLldpLocPortDesc[] = {1,0,8802,1,1,2,1,3,7,1,4};
constexpr size_t kLldpLocColLen = 11;

// IEEE 802.1AB LldpSystemCapabilitiesMap, encoded as BITS: bit N of the map lives
// in byte (N/8), mask (0x80 >> (N%8)). "wlanAccessPoint" is bit 3 -> byte 0 / 0x10.
constexpr size_t  kCapWlanAccessPointByte = 0;
constexpr uint8_t kCapWlanAccessPointMask = 0x10;

} // namespace

std::vector<LldpNeighbor> LldpProtocol::walkNeighbors(void* sessp)
{
    std::vector<LldpNeighbor> out;
    if (sessp == nullptr)
        return out;

    // Local port names: lldpLocPortNum -> port desc.
    std::map<uint32_t, std::string> locPortName;
    walkColumn(sessp, kLldpLocPortDesc, kLldpLocColLen, [&](netsnmp_variable_list* var) {
        if (var->name_length > kLldpLocColLen)
            locPortName[static_cast<uint32_t>(var->name[kLldpLocColLen])] = octetDisplay(var);
    });

    // Remote table, keyed by (localPortNum, remIndex).
    std::map<std::pair<uint32_t, uint32_t>, LldpNeighbor> byKey;
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
    walkColumn(sessp, kLldpRemChassisIdSubtype, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        byKey[keyOf(var)].remoteChassisIdSubtype = static_cast<int>(varInt(var));
    });
    walkColumn(sessp, kLldpRemSysCapEnabled, kLldpRemColLen, [&](netsnmp_variable_list* var) {
        if (var->type != ASN_OCTET_STR || var->val.string == nullptr || var->val_len == 0)
            return;

        auto& n = byKey[keyOf(var)];
        n.remoteCapabilitiesEnabled = var->val.string[0];
        if (var->val_len > 1)
            n.remoteCapabilitiesEnabled =
                static_cast<uint16_t>((n.remoteCapabilitiesEnabled << 8) | var->val.string[1]);

        n.remoteIsWlanAccessPoint =
            var->val_len > kCapWlanAccessPointByte &&
            (var->val.string[kCapWlanAccessPointByte] & kCapWlanAccessPointMask) != 0;
    });

    out.reserve(byKey.size());
    for (auto& [key, n] : byKey)
    {
        n.localPort = key.first;
        auto it = locPortName.find(key.first);
        if (it != locPortName.end())
            n.localPortName = it->second;
        out.push_back(std::move(n));
    }
    LOG_TRACE("walked LLDP neighbors (count={})", out.size());
    return out;
}

} // namespace pz::scand
