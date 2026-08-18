#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pz::mgmtd
{

// The egress handler for the pretzel-ai gRPC transport — the gRPC analogue of IpcClientHandler.
// It exists so the chat turn keeps the same shape it had over IPC: the HTTP thread dispatches a
// turn and returns a ticket immediately, and the answer is delivered later, on the main loop
// thread, exactly where an IPC ChatResponse would have arrived.
//
// A turn's gRPC call blocks for as long as the model takes, so it runs on a worker thread — mgmtd
// otherwise runs a single event loop, and blocking it would freeze the whole console. The result
// does NOT cross back on that worker thread: it is parked on a completion queue and handed to the
// sink during drain(), which the core calls on the loop thread. That keeps every write to the
// ServiceManager on the one thread it was designed for, mutex-free.
//
// This header is deliberately free of any gRPC/protobuf type, so the router and controller that
// use it never pull the generated headers into their translation units.
class GrpcClientHandler
{
public:
    // Delivered on the drain (main loop) thread: file this turn's document under its ticket.
    using ResultSink = std::function<void(std::uint32_t ticket, std::string resultJson)>;

    // target is host:port, e.g. "127.0.0.1:50051". workers bounds concurrent in-flight turns.
    explicit GrpcClientHandler(std::string target, std::size_t workers = 4);
    ~GrpcClientHandler();

    GrpcClientHandler(const GrpcClientHandler&) = delete;
    GrpcClientHandler& operator=(const GrpcClientHandler&) = delete;

    // Where completed turns are filed. Wired once at startup; invoked only from drain().
    void setResultSink(ResultSink sink);

    // Dispatch one turn. Returns immediately; the answer arrives later via poll().
    void egress(std::uint32_t ticket,
                std::string model,
                std::string message,
                std::string systemPrompt);

    // Pump this transport, alongside IpcClient::poll() and HttpServer::poll() in
    // MgmtdProcess::tick(). Peeks the completion queue and, when a worker has finished a turn,
    // drains it to the sink on the calling (main loop) thread. Cheap and non-blocking when idle.
    void poll();

private:
    // Deliver every completed turn to the sink. Split from poll() so the common idle path is a
    // cheap peek and the delivery only runs when there is something to deliver.
    void drain();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
