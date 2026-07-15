#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

struct LldpNeighbor
{
    uint32_t localPort{0};
    std::string localPortName;
    std::string remoteSysName;
    std::string remoteSysDescr;
    std::string remotePortId;
    std::string remoteChassisId;

    int remoteChassisIdSubtype{0};

    uint16_t remoteCapabilitiesEnabled{0};

    bool remoteIsWlanAccessPoint{false};
};

}
