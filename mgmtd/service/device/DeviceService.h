#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pz::mgmtd
{

struct DeviceInterface
{
    std::string ip;
    std::string netmask;
    std::uint32_t ifIndex{0};
    std::string ifName;
};

struct DeviceGroup
{
    std::string primaryIp;
    std::vector<std::string> ips;
    std::vector<std::string> interfaceMacs;
    std::vector<DeviceInterface> interfaces;

    std::string type{"unknown"};
    std::string subtype{"unknown"};

    std::string hostname;
    std::string sysDescr;
    std::string sysObjectId;
    std::string sysContact;
    std::string sysLocation;
    std::uint32_t sysUpTimeTicks{0};

    std::string vendor;
    std::string hostMac;

    nlohmann::json ifTable{nlohmann::json::array()};
    nlohmann::json lldpNeighbors{nlohmann::json::array()};

    bool hasSnmp{false};
};

class DeviceService
{
public:
    std::vector<DeviceGroup> groups() const;
};

}
