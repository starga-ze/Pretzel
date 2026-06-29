#pragma once

#include "snmp/SnmpTypes.h"

#include <string>

// Forward-declare net-snmp types to keep this header free of net-snmp macros.
struct snmp_session;
struct snmp_pdu;

namespace pz::scand
{

// Owns the SNMP version split and the MIB-II sysGroup knowledge.
//
// net-snmp owns the wire codec (BER encode/decode) and transport, so unlike
// icmpd there is no hand-rolled SnmpCodec/SnmpPacket — those layers live in
// the library. What is genuinely ours is: how a v2c vs v3 session is built,
// and which OIDs we ask for / how we parse them back. That is this class.
class SnmpProtocol
{
public:
    // Configure an already snmp_sess_init'd session for SNMPv2c. `community` is
    // resolved per-IP by the caller (SnmpScanConfig::communityFor); cfg supplies
    // the shared probe timeout/retries.
    static void configureV2c(snmp_session& sess,
                             const std::string& peer,
                             const std::string& community,
                             const SnmpScanConfig& cfg);

    // Configure a session for SNMPv3 (USM) using the given credentials (resolved
    // per-IP by the caller). cfg supplies the shared timeout/retries. Derives
    // auth/priv keys from the passphrases. Returns false if credentials are
    // incomplete or key generation fails.
    static bool configureV3(snmp_session& sess,
                            const std::string& peer,
                            const SnmpScanConfig& cfg,
                            const SnmpV3Config& v3);

    // Build a GET PDU for the MIB-II system group (sysDescr .. sysLocation).
    // Caller passes ownership to snmp_sess_send (or frees on failure).
    static snmp_pdu* buildSysGroupGet();

    // Parse a sysGroup response PDU's varbinds into `dev`.
    static void parseSysGroup(snmp_pdu* pdu, SnmpDevice& dev);

    // Walk ifPhysAddress (1.3.6.1.2.1.2.2.1.6) on an OPEN single-session handle
    // (the void* returned by snmp_sess_open) and collect the non-empty interface
    // MACs into dev.interfaceMacs. Synchronous (GETNEXT loop) — call from the v3
    // worker path. `sessp` is net-snmp's opaque session pointer.
    static void walkIfPhysAddr(void* sessp, SnmpDevice& dev);

    // Walk the ip-MIB ipAddrTable (ipAdEntIfIndex/ipAdEntNetMask, 1.3.6.1.2.1.4.20.1)
    // on an OPEN single-session handle and collect each IP-bearing interface
    // (ip + netmask + ifIndex + ifDescr name) into dev.interfaces. This surfaces a
    // firewall's WAN/LAN interface IPs, a switch's SVIs, router interfaces, etc.
    // Synchronous (GETNEXT walk + targeted ifDescr GETs).
    static void walkInterfaceAddrs(void* sessp, SnmpDevice& dev);

    // Walk IF-MIB ifTable + ifXTable into dev.ifTable (interface inventory).
    static void walkIfTable(void* sessp, SnmpDevice& dev);

    // LLDP (lldpRemTable) is walked by lldp::LldpProtocol, not here — see
    // lldp/LldpProtocol.h.

    // Walk ip-MIB ipNetToMediaTable (ARP) into dev.arpEntries (IP↔MAC).
    static void walkArpTable(void* sessp, SnmpDevice& dev);
};

} // namespace pz::scand
