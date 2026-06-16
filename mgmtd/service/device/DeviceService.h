#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pz::mgmtd
{

// One IP-bearing interface (from probe_devices.interfaces / ipAddrTable / vendor API).
struct DeviceInterface
{
    std::string   ip;
    std::string   netmask;
    std::uint32_t ifIndex{0};
    std::string   ifName;
};

// One logical device. Multiple IPs that share an interface MAC (ifPhysAddress) are
// folded into a single group; SNMP-less hosts (ICMP-alive only) come through as
// singletons with has_snmp=false.
//
// type taxonomy (3-way):
//   "network" — SNMP-managed network gear (router/switch/firewall/ap/gateway)
//   "server"  — SNMP-managed endpoint (hypervisor/bmc/server/windows/linux/printer)
//   "host"    — ICMP-reachable only, no SNMP (general endpoint, e.g. laptop/PC)
struct DeviceGroup
{
    std::string              primaryIp;       // representative IP (SNMP-bearing first, lowest IP)
    std::vector<std::string> ips;             // all IPs in the group (sorted)
    std::vector<std::string> interfaceMacs;   // union of interface MACs (sorted, deduped)
    std::vector<DeviceInterface> interfaces;  // union of IP-bearing interfaces

    std::string type{"host"};      // "network" | "server" | "host"
    std::string subtype{"unknown"};// router/switch/firewall/ap/gateway/hypervisor/bmc/server/windows/linux/printer/unknown

    std::string   hostname;        // sys_name
    std::string   sysDescr;
    std::string   sysObjectId;
    std::string   sysContact;
    std::string   sysLocation;
    std::uint32_t sysUpTimeTicks{0};

    std::string   vendor;          // engined-resolved (sysObjectID PEN, or host OUI)
    std::string   hostMac;         // ICMP-only host MAC (from a switch/router ARP table)

    // Pass-through SNMP detail (stored/emitted verbatim) — populated for the primary.
    nlohmann::json ifTable{nlohmann::json::array()};        // IF-MIB ifTable/ifXTable
    nlohmann::json lldpNeighbors{nlohmann::json::array()};  // LLDP topology edges

    bool hasSnmp{false};
};

// Read-only inventory view assembled from probe_devices (ICMP reachability + SNMP/API
// attributes incl. interface_macs, all written by engined). Groups by MAC fingerprint
// and classifies each device as network gear vs general host.
class DeviceService
{
public:
    // Returns all device groups, sorted network-first then host, each by primary IP.
    std::vector<DeviceGroup> groups() const;
};

} // namespace pz::mgmtd
