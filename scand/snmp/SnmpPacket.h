#pragma once

#include "snmp/SnmpTypes.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace pz::scand
{

class SnmpPacket final
{
public:
    SnmpPacket() = default;

    explicit SnmpPacket(std::vector<SnmpDevice> devices) : m_devices(std::move(devices))
    {
    }

    const std::vector<SnmpDevice>& devices() const
    {
        return m_devices;
    }
    std::vector<SnmpDevice>& devices()
    {
        return m_devices;
    }

    std::size_t size() const
    {
        return m_devices.size();
    }
    bool empty() const
    {
        return m_devices.empty();
    }

private:
    std::vector<SnmpDevice> m_devices;
};

}
