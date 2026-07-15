#pragma once

#include "snmp/SnmpTypes.h"

#include <string>

struct snmp_session;
struct snmp_pdu;

namespace pz::scand
{

class SnmpProtocol
{
public:
    static void configureV2c(snmp_session& sess, const std::string& peer, const std::string& community,
                             const SnmpScanConfig& cfg);

    static bool configureV3(snmp_session& sess, const std::string& peer, const SnmpScanConfig& cfg,
                            const SnmpV3Config& v3);

    static snmp_pdu* buildSysGroupGet();

    static void parseSysGroup(snmp_pdu* pdu, SnmpDevice& dev);

    static void walkIfPhysAddr(void* sessp, SnmpDevice& dev);

    static void walkInterfaceAddrs(void* sessp, SnmpDevice& dev);

    static void walkIfTable(void* sessp, SnmpDevice& dev);

    static void walkArpTable(void* sessp, SnmpDevice& dev);
};

}
