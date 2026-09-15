#pragma once

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pz::http
{

// Outbound HTTPS with certificate pinning, for reaching managed devices. collectord is the only
// daemon that drives it — it owns every vendor HTTP call (connector tests, credential keygen and
// refresh, SASE health, periodic collection), while probed stays ICMP-only and holds no
// credentials. The transport still lives in shared, alongside the inbound HttpServer that apid and
// mgmtd share, rather than inside the one daemon that happens to call it today.
//
// Devices like PAN-OS ship a self-signed certificate whose CN is the chassis serial, so hostname
// verification can never pass and the usual answer — disabling verification — leaves the
// connection open to an active man-in-the-middle. Instead the caller pins the peer's SHA-256
// fingerprint: trust-on-first-use, then exact match on every later connect.
//
// The pin is enforced BEFORE the request is written, so credentials are never put on the wire to
// an unverified peer. A first contact (no pin recorded yet) therefore completes the handshake,
// reports the fingerprint, and stops — the operator confirms it, and only the next call carries
// the credential.
struct ClientRequest
{
    std::string host;
    std::uint16_t port{443};
    std::string method{"GET"};
    std::string target{"/"};
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Empty = first contact: handshake, report the fingerprint, send nothing.
    std::string expectedFingerprint;
    // true = the device was re-certified by a CA; verify the chain and hostname instead.
    bool verifyCa{false};

    std::chrono::milliseconds timeout{std::chrono::seconds(8)};
};

struct ClientResponse
{
    bool tlsOk{false};          // TCP + TLS handshake completed
    bool pinMatched{false};     // peer fingerprint equals expectedFingerprint
    bool requestSent{false};    // false when the pin gate stopped us before writing
    int status{0};              // HTTP status, when the request was sent
    std::string body;
    std::string error;          // transport/TLS failure detail, empty on success
    std::string fingerprint;    // peer certificate SHA-256, "AA:BB:.." uppercase
    std::string certSubject;
};

// Runs the exchange on `ioc` and calls `onDone` exactly once when it settles — success, transport
// failure, pin refusal or timeout alike. The handler runs inside whatever pumps the context (for
// the daemons, io_context::poll() from the tick loop), i.e. on the main thread, so it may touch
// service state, routers and the event queue directly.
//
// The call returns immediately. The session keeps itself alive for the duration, so the caller
// need not hold anything; `ioc` must outlive the exchange.
using ResponseHandler = std::function<void(ClientResponse)>;

void requestAsync(boost::asio::io_context& ioc, ClientRequest req, ResponseHandler onDone);

// Blocking form: runs the same exchange on a private io_context and returns when it settles. The
// per-phase deadlines still bound it, so it cannot hang longer than ClientRequest::timeout.
//
// Use this ONLY where the caller has no event loop to pump and is expected to stall — authd's
// OIDC token exchange and JWKS fetch, which sit inside a synchronous request/response path. A
// daemon with a tick loop must use requestAsync instead: blocking there stops every other service
// in the process, not just the one making the call.
ClientResponse requestSync(ClientRequest req);

}
