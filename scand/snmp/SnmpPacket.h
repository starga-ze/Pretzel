#pragma once

#include "snmp/SnmpTypes.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace pz::scand
{

// Typed container for a completed SNMP scan sweep. The SnmpEngineHandler wraps the
// decoded results into an SnmpPacket and hands it through the RxRouter to the event
// factory — mirroring icmpd's IcmpPacket so the router never deals in raw STL or
// service-layer types. Result-only for now; the natural extension point for scan
// metadata (scan id, source request, timings).
class SnmpPacket final
{
public:
    SnmpPacket() = default;

    explicit SnmpPacket(std::vector<SnmpDevice> devices)
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
