#pragma once

#include <string>
#include <unordered_map>

namespace pz::engined
{

// Resolves device vendors two ways, owned by engined (the processing brain):
//   - vendorForSysObjectId(): SNMP sysObjectID enterprise number → vendor (small
//     curated PEN table). Lets SNMP devices that leave sysDescr empty (e.g. iDRAC)
//     still be identified.
//   - vendorForMac(): MAC 24-bit OUI → vendor, from the bundled IEEE OUI table.
//     Lets SNMP-less hosts (learned via a switch/router ARP table) be identified.
class VendorResolver
{
public:
    // Loads the IEEE OUI table from the first existing candidate path. Safe to call
    // once at startup; missing file just means vendorForMac() returns "".
    void loadOui();

    std::string vendorForMac(const std::string& mac) const;
    std::string vendorForSysObjectId(const std::string& sysObjectId) const;

    std::size_t ouiCount() const { return m_oui.size(); }

private:
    std::unordered_map<std::string, std::string> m_oui;  // "aabbcc" (lowercase) → vendor
};

} // namespace pz::engined
