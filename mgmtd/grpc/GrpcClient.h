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

    // --- The tech-doc knowledge base ---------------------------------------------------------
    //
    // These return the server's reply already rendered as JSON rather than as a struct. mgmtd has
    // no opinion about a crawl: it shows the operator what pretzel-ai reported and posts back what
    // the operator decided, so a field added on the pretzel-ai side reaches the console without a
    // change here. It is the same reason ChatController serves result_json verbatim.

    // What a refresh would do. Seconds — one sitemap fetch, no page bodies.
    std::string checkCorpus(const std::string& scope, std::string& error);

    // What the store holds right now, for the card's resting state.
    std::string corpusStatus(std::string& error);

    // Runs the crawl, calling on_progress once per progress message. Blocks for as long as the
    // crawl takes, so callers run it on a worker thread. Returning early from on_progress is not
    // how this is cancelled — dropping the reader is, which the caller does by destroying it.
    void refreshCorpus(const std::string& scope,
                       const std::function<void(const std::string&)>& on_progress,
                       std::string& error);

    // Cancels the refresh currently in flight, from another thread. A no-op when none is running.
    // The server sees the cancelled stream and stops crawling (its generator checks is_active), so
    // this stops work on the appliance rather than merely stopping us listening to it.
    void cancelRefresh();

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
