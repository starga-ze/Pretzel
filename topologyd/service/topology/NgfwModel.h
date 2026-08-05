#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pz::topologyd::ngfw
{

// The NGFW domain knowledge: how to read a PAN-OS configuration document and say what a thing IS.
//
// Kept out of TopologyService so the composer stays a composer. Everything here is a pure function
// of vendor JSON — no database, no config, no IPC — which is what makes the classification rules
// readable on their own and testable without a firewall.
//
// The rules are stated as functions rather than tables because each one is a judgement with a
// reason, and the reason is the part that has to survive the next PAN-OS release.

// ── Interface role ────────────────────────────────────────────────────────────────────────────
//
// What an interface is FOR. PAN-OS does not record this — a firewall has zones and virtual routers,
// and neither is in the interface document — so it has to be inferred, and the inference has to be
// honest about being one. Two signals, in order of strength:
//
//   1. The address. A public address on a firewall interface is an internet edge in every
//      deployment worth drawing; RFC1918 is inside. It is an inference, but the reliable one.
//   2. What terminates on it. An interface named as an IKE gateway's local-address carries site
//      VPN; one named by a GlobalProtect portal or gateway faces remote users.
//
// The two disagree more often than they look like they should, and the disagreement is meaningful
// rather than an error: a firewall behind an upstream NAT terminates its VPN and its GlobalProtect
// portal on a private address. Forcing that interface to WAN would print an RFC1918 address under an
// internet-edge label; forcing it to LAN would hide that the outside world arrives there. So it gets
// its own role. `Edge` means exactly "inside address, externally-reachable services terminate here",
// which is a thing an operator needs to see and neither of the other two says.
//
// `loopback`/`tunnel` are named by PAN-OS itself and need no inference.
enum class IfRole
{
    Wan,        // internet edge — a public address
    Edge,       // private address, but a VPN or GlobalProtect endpoint terminates here
    Lan,        // inside — private address, nothing external terminating
    Loopback,
    Tunnel,
    Unknown,    // no address configured: it is cabled to nothing we can see
};

const char* ifRoleName(IfRole r);

// True for RFC1918, CGNAT (100.64/10), link-local and loopback ranges — "not routable on the
// internet", which is the question the WAN/LAN split actually turns on.
bool isPrivateV4(const std::string& cidrOrIp);

// ── Peer classification ───────────────────────────────────────────────────────────────────────
//
// The single most valuable field the firewall gives us. An IKE gateway's peer address says what is
// on the other end of the tunnel, and when the other end is Prisma Access the FQDN spells it out:
//
//   sherpain-hq.south-korea.sc.osy5synss5.gpcloudservice.com   Service Connection
//   south-korea-aspen.rn.nnjy2ycyn.gpcloudservice.com          Remote Network
//
// The `sc`/`rn` label is the connection type, the label after it is the TENANT, and the leading
// label carries the location. That tenant token is what makes a hub knowable: two firewalls whose
// Service Connections carry the same tenant token are two spokes of one Prisma tenant, and nothing
// else in the collected data says so.
//
// A bare IP peer is somebody else's device — a branch router, a partner, a cloud VPN gateway. It is
// drawn as an external peer rather than guessed at, because guessing is how a picture starts lying.
enum class PeerKind
{
    ServiceConnection,
    RemoteNetwork,
    External,      // an address or FQDN that is not Prisma Access
    Unknown,       // nothing configured
};

const char* peerKindName(PeerKind k);

struct Peer
{
    PeerKind kind{PeerKind::Unknown};
    std::string addr;      // exactly what the configuration says, verbatim
    std::string label;     // the readable part: "sherpain-hq", "south-korea-aspen"
    std::string region;    // "south-korea" when the FQDN carries one
    std::string tenant;    // the Prisma tenant token — the join key between spokes
};

Peer classifyPeer(const std::string& addr);

// ── Document readers ──────────────────────────────────────────────────────────────────────────

// PAN-OS REST wraps everything as {"result":{"entry":[…]}} and collapses a one-element list to an
// object. Normalised to a list once so no caller has to remember which shape it got.
nlohmann::json entriesOf(const nlohmann::json& doc);

// A field as a string, whatever JSON type the vendor used for it.
std::string str(const nlohmann::json& o, const char* key);

// Every address configured on an interface. PAN-OS nests them per mode (layer3/layer2/…) and each
// address is an `entry` whose NAME is the address — "10.0.0.1/24" is the key, not a value.
std::vector<std::string> interfaceAddresses(const nlohmann::json& entry);

// Which mode an interface is configured in — the closest the config document comes to saying what
// the interface is for, and worth showing beside the address.
std::string interfaceMode(const nlohmann::json& entry);

// Admin state from the configuration. NOT operational state: the config API cannot say whether a
// cable is plugged in, so the model reports `admin_state` and the page says so rather than printing
// "up" over a link that might be dark.
std::string adminState(const nlohmann::json& entry);

// ── Parsed sub-documents ──────────────────────────────────────────────────────────────────────

// One IKE gateway: which interface it leaves by, and what is on the far end.
struct IkeGateway
{
    std::string name;
    std::string interfaceName;
    std::string localIp;
    Peer peer;
    std::string version;    // ikev1 / ikev2
};

std::vector<IkeGateway> readIkeGateways(const nlohmann::json& doc);

// One GlobalProtect portal: where it listens, and which gateways it hands clients.
struct GpPortal
{
    std::string name;
    std::string interfaceName;
    std::string localIp;
    std::vector<std::string> gateways;   // "name @ address", as the portal advertises them
};

std::vector<GpPortal> readGpPortals(const nlohmann::json& doc);

// One GlobalProtect gateway: where it terminates users, and the pools it hands them.
struct GpGateway
{
    std::string name;
    std::string interfaceName;
    std::string localIp;
    bool tunnelMode{false};
    std::vector<std::string> pools;
};

std::vector<GpGateway> readGpGateways(const nlohmann::json& doc);

}
