#pragma once

#include "lldp/LldpTypes.h"

#include <vector>

namespace pz::scand
{

// Owns LLDP-MIB knowledge (which OIDs to walk, how to parse them back), kept
// separate from SnmpProtocol the same way icmpd splits its protocol concerns
// across dedicated files. LLDP rides on the same open net-snmp session as the
// sysGroup/ifTable/ARP walks (no extra round trip), but identifying a directly-
// connected neighbor (capability bits, chassis MAC) is a distinct concern from
// polling the local device's own MIB-II/IF-MIB data.
class LldpProtocol
{
public:
    // Walk the LLDP-MIB lldpRemTable (+ lldpLocPortTable for local port names) on
    // an OPEN single-session handle (the void* returned by snmp_sess_open).
    // Synchronous (GETNEXT loop) — call from the v2c reap path or the v3 worker
    // path, same as SnmpProtocol's walkers. `sessp` is net-snmp's opaque session
    // pointer.
    static std::vector<LldpNeighbor> walkNeighbors(void* sessp);
};

} // namespace pz::scand
