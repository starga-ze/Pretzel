#pragma once

#include "snmp/SnmpTypes.h"

#include <string>

// Forward-declare net-snmp types to keep this header free of net-snmp macros.
struct snmp_session;
struct snmp_pdu;

namespace pz::snmpd
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
    // Configure an already snmp_sess_init'd session for SNMPv2c.
    static void configureV2c(snmp_session& sess,
                             const std::string& peer,
                             const SnmpScanConfig& cfg);

    // Configure a session for SNMPv3 (USM). Derives auth/priv keys from the
    // passphrases. Returns false if credentials are incomplete or key
    // generation fails.
    static bool configureV3(snmp_session& sess,
                            const std::string& peer,
                            const SnmpScanConfig& cfg);

    // Build a GET PDU for the MIB-II system group (sysDescr .. sysLocation).
    // Caller passes ownership to snmp_sess_send (or frees on failure).
    static snmp_pdu* buildSysGroupGet();

    // Parse a sysGroup response PDU's varbinds into `dev`.
    static void parseSysGroup(snmp_pdu* pdu, SnmpDevice& dev);
};

} // namespace pz::snmpd
