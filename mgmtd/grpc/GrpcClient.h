#pragma once

#include <functional>
#include <memory>
#include <string>

namespace pz::mgmtd
{

// The mgmtd-side client for the pretzel-ai inference service — the gRPC replacement for the
// old IPC ChatRequest/ChatResponse path to inferd. A chat turn streams back chunk by chunk;
// on_delta is invoked for each piece of text as it arrives, which is what lets the console
// render the answer as it is produced rather than after it is complete.
//
// Pimpl so this header stays free of the generated protobuf/gRPC headers: only the .cpp (and
// the smoke target that compiles it) needs them.
class GrpcClient
{
public:
    // target is host:port, e.g. "127.0.0.1:50051". The channel is created lazily/insecure —
    // loopback only for now (see the SSL note in script/install.py install_grpc()).
    explicit GrpcClient(const std::string& target);
    ~GrpcClient();

    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    struct Outcome
    {
        // A human-readable failure reason, or empty on success. Set for a transport failure, or
        // for the turn-level error the server reported on its final chunk.
        std::string error;
        // The complete turn document (reply, AIRS scan, usage, ok/code) the server put on its
        // final chunk. Empty only when the stream never reached that chunk (a transport failure).
        std::string resultJson;
    };

    // Streams one chat turn. on_delta is called once per non-empty chunk, in order.
    Outcome chat(const std::string& model,
                 const std::string& message,
                 const std::string& systemPrompt,
                 const std::function<void(const std::string&)>& on_delta);

    // Channel connectivity, for connection logging. The state is grpc_connectivity_state as a
    // plain int (IDLE=0, CONNECTING=1, READY=2, TRANSIENT_FAILURE=3, SHUTDOWN=4) so the header
    // stays free of gRPC types. tryToConnect nudges an idle channel to start connecting.
    int connectivityState(bool tryToConnect);
    // Block until the state leaves lastState or timeoutMs elapses; true if it changed. This is how
    // a monitor observes connect / drop / reconnect without polling in a tight loop.
    bool waitForStateChange(int lastState, int timeoutMs);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
