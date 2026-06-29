#pragma once

#include "snmp/SnmpTypes.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace pz::scand
{

// Typed container for a completed vendor-API scan sweep. Mirrors SnmpPacket
// (scand/snmp/SnmpPacket.h) — ApiEngineHandler wraps ApiEngine's decoded results
// into one of these and hands it through the RxRouter to the event factory, never
// constructed inline in the router.
class ApiPacket final
{
public:
    ApiPacket() = default;

    explicit ApiPacket(std::vector<SnmpDevice> devices)
        : m_devices(std::move(devices))
    {
    }

    const std::vector<SnmpDevice>& devices() const { return m_devices; }
    std::vector<SnmpDevice>&       devices()       { return m_devices; }

    std::size_t size() const  { return m_devices.size(); }
    bool        empty() const { return m_devices.empty(); }

private:
    std::vector<SnmpDevice> m_devices;
};

} // namespace pz::scand
