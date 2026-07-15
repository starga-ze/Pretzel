#include "api/PaloAltoApiProvider.h"
#include "api/HttpsClient.h"

#include "util/Logger.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <sstream>
#include <unordered_map>

namespace pt = boost::property_tree;

namespace pz::scand
{
namespace
{

bool splitCidr(const std::string& cidr, std::string& ip, int& prefix)
{
    if (cidr.empty() || cidr == "N/A")
        return false;
    const auto slash = cidr.find('/');
    if (slash == std::string::npos)
        return false;
    ip = cidr.substr(0, slash);
    try
    {
        prefix = std::stoi(cidr.substr(slash + 1));
    }
    catch (...)
    {
        return false;
    }
    return prefix >= 0 && prefix <= 32 && !ip.empty();
}

std::string prefixToNetmask(int prefix)
{
    const uint32_t mask = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (mask >> 24) & 0xFF, (mask >> 16) & 0xFF, (mask >> 8) & 0xFF,
                  mask & 0xFF);
    return buf;
}

uint64_t parseSpeed(const std::string& s)
{
    try
    {
        return static_cast<uint64_t>(std::stoull(s));
    }
    catch (...)
    {
        return 0;
    }
}

template <typename Fn> bool withResult(const std::string& xml, Fn&& fn)
{
    try
    {
        pt::ptree root;
        std::istringstream is(xml);
        pt::read_xml(is, root, pt::xml_parser::no_comments);

        const auto status = root.get<std::string>("response.<xmlattr>.status", "");
        if (status != "success")
            return false;

        const auto result = root.get_child_optional("response.result");
        if (!result)
            return false;

        fn(*result);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("XML parse error (error={})", e.what());
        return false;
    }
}

}

std::string PaloAltoApiProvider::resolveApiKey(const ApiCredential& cred, const std::string& host)
{
    if (!cred.apiKey.empty())
        return cred.apiKey;

    const std::string body = "type=keygen&user=" + HttpsClient::urlEncode(cred.username) +
                             "&password=" + HttpsClient::urlEncode(cred.password);

    const auto resp = HttpsClient::postForm(host, cred.port, "/api/", body, cred.timeoutMs, cred.verifyTls);
    if (!resp.ok)
    {
        LOG_WARN("keygen failed (host={}, status={}, error={})", host, resp.status, resp.error);
        return {};
    }

    std::string key;
    withResult(resp.body, [&](const pt::ptree& result) { key = result.get<std::string>("key", ""); });
    return key;
}

std::string PaloAltoApiProvider::op(const ApiCredential& cred, const std::string& host, const std::string& apiKey,
                                    const std::string& xmlCmd)
{
    const std::string target =
        "/api/?type=op&cmd=" + HttpsClient::urlEncode(xmlCmd) + "&key=" + HttpsClient::urlEncode(apiKey);
    const auto resp = HttpsClient::get(host, cred.port, target, cred.timeoutMs, cred.verifyTls);
    if (!resp.ok)
    {
        LOG_WARN("op failed (host={}, status={}, error={})", host, resp.status, resp.error);
        return {};
    }
    return resp.body;
}

void PaloAltoApiProvider::parseInterfaces(const std::string& xml, SnmpDevice& dev)
{
    std::vector<SnmpIfEntry> ifTable;
    std::vector<SnmpInterface> interfaces;
    std::unordered_map<std::string, uint32_t> nameToIdx;

    withResult(xml,
               [&](const pt::ptree& result)
               {
                   if (const auto hw = result.get_child_optional("hw"))
                   {
                       for (const auto& kv : *hw)
                       {
                           if (kv.first != "entry")
                               continue;
                           const auto& e = kv.second;

                           SnmpIfEntry ent;
                           ent.name = e.get<std::string>("name", "");
                           ent.descr = ent.name;
                           ent.ifIndex = e.get<uint32_t>("id", 0);
                           ent.mac = e.get<std::string>("mac", "");
                           ent.speed = parseSpeed(e.get<std::string>("speed", ""));
                           ent.operStatus = (e.get<std::string>("state", "") == "up") ? 1 : 2;
                           if (!ent.name.empty())
                               nameToIdx[ent.name] = ent.ifIndex;
                           ifTable.push_back(std::move(ent));
                       }
                   }

                   if (const auto ifnet = result.get_child_optional("ifnet"))
                   {
                       for (const auto& kv : *ifnet)
                       {
                           if (kv.first != "entry")
                               continue;
                           const auto& e = kv.second;

                           const std::string name = e.get<std::string>("name", "");
                           const uint32_t idx = e.get<uint32_t>("id", 0);
                           if (idx != 0 && !name.empty())
                               nameToIdx.emplace(name, idx);

                           auto addIp = [&](const std::string& cidr)
                           {
                               std::string ip;
                               int prefix = 0;
                               if (!splitCidr(cidr, ip, prefix))
                                   return;
                               SnmpInterface itf;
                               itf.ip = ip;
                               itf.netmask = prefixToNetmask(prefix);
                               itf.ifIndex = idx;
                               itf.ifName = name;
                               interfaces.push_back(std::move(itf));
                           };

                           addIp(e.get<std::string>("ip", ""));
                           if (const auto addr = e.get_child_optional("addr"))
                               for (const auto& m : *addr)
                                   if (m.first == "member")
                                       addIp(m.second.get_value<std::string>(""));
                       }
                   }
               });

    if (!ifTable.empty())
    {
        dev.ifTable = std::move(ifTable);
    }
    if (!interfaces.empty())
        dev.interfaces = std::move(interfaces);
}

void PaloAltoApiProvider::parseArp(const std::string& xml, SnmpDevice& dev)
{
    std::unordered_map<std::string, uint32_t> nameToIdx;
    for (const auto& e : dev.ifTable)
        if (!e.name.empty())
            nameToIdx[e.name] = e.ifIndex;

    std::vector<SnmpArpEntry> arp;
    withResult(xml,
               [&](const pt::ptree& result)
               {
                   const auto entries = result.get_child_optional("entries");
                   if (!entries)
                       return;
                   for (const auto& kv : *entries)
                   {
                       if (kv.first != "entry")
                           continue;
                       const auto& e = kv.second;

                       const std::string mac = e.get<std::string>("mac", "");
                       if (mac.empty() || mac == "(incomplete)")
                           continue;

                       SnmpArpEntry a;
                       a.ip = e.get<std::string>("ip", "");
                       a.mac = mac;
                       const std::string iface = e.get<std::string>("interface", "");
                       const auto it = nameToIdx.find(iface);
                       a.ifIndex = (it != nameToIdx.end()) ? it->second : 0;
                       if (!a.ip.empty())
                           arp.push_back(std::move(a));
                   }
               });

    if (!arp.empty())
        dev.arpEntries = std::move(arp);
}

void PaloAltoApiProvider::parseLldp(const std::string& xml, SnmpDevice& dev)
{
    std::vector<LldpNeighbor> neighbors;
    withResult(xml,
               [&](const pt::ptree& result)
               {
                   for (const auto& kv : result)
                   {
                       if (kv.first != "entry")
                           continue;
                       const auto& portEntry = kv.second;

                       const std::string localName = portEntry.get<std::string>("<xmlattr>.name", "");
                       const uint32_t localIdx = portEntry.get<uint32_t>("local.index", 0);

                       const auto nbrs = portEntry.get_child_optional("neighbors");
                       if (!nbrs)
                           continue;
                       for (const auto& nkv : *nbrs)
                       {
                           if (nkv.first != "entry")
                               continue;
                           const auto& n = nkv.second;

                           LldpNeighbor nb;
                           nb.localPort = localIdx;
                           nb.localPortName = localName;
                           nb.remoteChassisId = n.get<std::string>("chassis-id", "");
                           nb.remotePortId = n.get<std::string>("port-id", "");
                           nb.remoteSysName = n.get<std::string>("system-name", "");
                           nb.remoteSysDescr = n.get<std::string>("system-description", "");
                           neighbors.push_back(std::move(nb));
                       }
                   }
               });

    if (!neighbors.empty())
        dev.lldpNeighbors = std::move(neighbors);
}

void PaloAltoApiProvider::parseSystemInfo(const std::string& xml, SnmpDevice& dev)
{
    withResult(xml,
               [&](const pt::ptree& result)
               {
                   const auto sys = result.get_child_optional("system");
                   if (!sys)
                       return;
                   if (dev.sysName.empty())
                       dev.sysName = sys->get<std::string>("hostname", "");
                   if (dev.sysDescr.empty())
                   {
                       const std::string model = sys->get<std::string>("model", "");
                       const std::string sw = sys->get<std::string>("sw-version", "");
                       if (!model.empty())
                           dev.sysDescr = "Palo Alto Networks " + model + " PAN-OS " + sw;
                   }
               });
}

bool PaloAltoApiProvider::collect(const ApiCredential& cred, SnmpDevice& dev)
{
    const std::string host = cred.host.empty() ? dev.ip : cred.host;

    const std::string apiKey = resolveApiKey(cred, host);
    if (apiKey.empty())
    {
        LOG_WARN("no API key — skipping (host={})", host);
        return false;
    }

    const size_t beforeIf = dev.interfaces.size();
    const size_t beforeArp = dev.arpEntries.size();

    if (auto xml = op(cred, host, apiKey, "<show><interface>all</interface></show>"); !xml.empty())
        parseInterfaces(xml, dev);

    if (auto xml = op(cred, host, apiKey, "<show><arp><entry name=\"all\"/></arp></show>"); !xml.empty())
        parseArp(xml, dev);

    if (auto xml = op(cred, host, apiKey, "<show><lldp><neighbors>all</neighbors></lldp></show>"); !xml.empty())
        parseLldp(xml, dev);

    if (dev.sysName.empty() || dev.sysDescr.empty())
        if (auto xml = op(cred, host, apiKey, "<show><system><info></info></system></show>"); !xml.empty())
            parseSystemInfo(xml, dev);

    const bool gained =
        dev.interfaces.size() != beforeIf || dev.arpEntries.size() != beforeArp || !dev.lldpNeighbors.empty();

    LOG_DEBUG("collected (host={}, interfaces={}, arp={}, lldp={})", host, dev.interfaces.size(), dev.arpEntries.size(),
              dev.lldpNeighbors.size());
    return gained;
}

}
