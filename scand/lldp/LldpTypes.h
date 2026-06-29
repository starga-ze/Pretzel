#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

// One LLDP neighbor (lldpRemTable) — a directly-connected device. The basis for L2
// topology edges, and — via remoteChassisIdSubtype + remoteIsWlanAccessPoint — for
// identifying an SNMP-silent neighbor (e.g. a wireless AP with no community
// configured) without ever polling that device directly: the switch it's plugged
// into already reports it.
struct LldpNeighbor
{
    uint32_t    localPort{0};      // lldpRemLocalPortNum (index)
    std::string localPortName;     // lldpLocPortId/Desc for that local port
    std::string remoteSysName;     // lldpRemSysName
    std::string remoteSysDescr;    // lldpRemSysDesc
    std::string remotePortId;      // lldpRemPortId
    std::string remoteChassisId;   // lldpRemChassisId

    // lldpRemChassisIdSubtype (RFC 2922 LldpChassisIdSubtype). Only when this is 4
    // ("macAddress") is remoteChassisId safe to join against a host's MAC (e.g.
    // probe_devices.mac from ICMP/ARP) — other subtypes (ifAlias, ifName, local,
    // ...) are not MAC strings even though they may render as printable text.
    int remoteChassisIdSubtype{0};

    // lldpRemSysCapEnabled raw bitmap (IEEE 802.1AB LldpSystemCapabilitiesMap),
    // kept for any future capability beyond AP (router, bridge, repeater, ...).
    uint16_t remoteCapabilitiesEnabled{0};

    // Capability bit "wlanAccessPoint" — vendor-neutral AP identification that
    // does not depend on sysDescr keyword matching.
    bool remoteIsWlanAccessPoint{false};
};

} // namespace pz::scand
