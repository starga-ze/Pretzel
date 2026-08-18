#include "grpc/GrpcClientHandler.h"

#include "grpc/GrpcClient.h"

#include "util/Logger.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

namespace
{

// grpc_connectivity_state as a name, for the connection log.
const char* stateName(int s)
{
    switch (s)
    {
    case 0: return "IDLE";
    case 1: return "CONNECTING";
    case 2: return "READY";
    case 3: return "TRANSIENT_FAILURE";
    case 4: return "SHUTDOWN";
    default: return "UNKNOWN";
    }
}

// A turn whose stream never reached the server's final chunk (the process is down, the connection
// dropped) still has to resolve the ticket the browser is polling — so synthesize the same
// document shape the gateway would have returned for an unreachable upstream.
std::string unreachableDoc(const std::string& reason)
{
    std::string escaped;
    escaped.reserve(reason.size());
    for (char c : reason)
    {
        if (c == '"' || c == '\\')
            escaped += '\\';
        if (c == '\n' || c == '\r' || c == '\t')
            escaped += ' ';
        else
            escaped += c;
    }
    return R"({"ok":false,"code":"UNREACHABLE","error":")" + escaped + R"("})";
}

}

struct GrpcClientHandler::Impl
{
    struct Task
    {
        std::uint32_t ticket;
        std::string model;
        std::string message;
        std::string systemPrompt;
    };

    // target is copied for the log line before it is moved into the client.
    explicit Impl(std::string t) : target(t), client(std::move(t)) {}

    std::string target;
    GrpcClient client;

    std::mutex taskMutex;
    std::condition_variable taskCv;
    std::deque<Task> tasks;
    std::atomic<bool> stopping{false};

    std::mutex doneMutex;
    std::deque<std::pair<std::uint32_t, std::string>> done;

    ResultSink sink;
    std::vector<std::thread> workers;
    std::thread monitor;

    void run()
    {
        for (;;)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(taskMutex);
                taskCv.wait(lock, [this] { return stopping.load() || !tasks.empty(); });
                if (stopping.load() && tasks.empty())
                    return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }

            // Deltas are dropped for now — the poll-based console reads only the final document.
            // The callback is where the future browser-side SSE stream will hook in.
            auto outcome = client.chat(task.model, task.message, task.systemPrompt,
                                       [](const std::string&) {});

            // A turn either reached the server (resultJson set — even a gateway failure is a
            // reply) or did not (transport failure). The latter is the connection-level event
            // worth a warning; the former is routine and stays at debug.
            if (outcome.resultJson.empty())
                LOG_WARN("pretzel-ai unreachable (ticket={}): {}", task.ticket,
                         outcome.error.empty() ? "no result" : outcome.error);
            else
                LOG_DEBUG("pretzel-ai answered (ticket={}, {} bytes)", task.ticket,
                          outcome.resultJson.size());

            std::string doc = !outcome.resultJson.empty()
                                  ? std::move(outcome.resultJson)
                                  : unreachableDoc(outcome.error.empty()
                                                       ? "pretzel-ai returned no result"
                                                       : outcome.error);
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                done.emplace_back(task.ticket, std::move(doc));
            }
        }
    }

    // Watches the gRPC channel and logs every connectivity transition — connect, drop, reconnect.
    // This is what makes a pretzel-ai restart visible from the mgmtd side: READY -> TRANSIENT_
    // FAILURE when it goes down, and back to READY when it returns, with no calls needed in between.
    void monitorLoop()
    {
        int last = client.connectivityState(/*tryToConnect=*/true);
        LOG_INFO("pretzel-ai gRPC channel ({}): {}", target, stateName(last));
        while (!stopping.load())
        {
            // Block up to 1s for a change, then re-check — the timeout also lets us notice stopping.
            client.waitForStateChange(last, 1000);
            if (stopping.load())
                break;
            const int now = client.connectivityState(/*tryToConnect=*/false);
            if (now != last)
            {
                LOG_INFO("pretzel-ai gRPC channel: {} -> {}", stateName(last), stateName(now));
                last = now;
            }
        }
    }
};

GrpcClientHandler::GrpcClientHandler(std::string target, std::size_t workers)
    : m_impl(std::make_unique<Impl>(std::move(target)))
{
    if (workers == 0)
        workers = 1;
    m_impl->workers.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i)
        m_impl->workers.emplace_back([this] { m_impl->run(); });

    m_impl->monitor = std::thread([this] { m_impl->monitorLoop(); });
}

GrpcClientHandler::~GrpcClientHandler()
{
    m_impl->stopping.store(true);
    m_impl->taskCv.notify_all();
    for (auto& t : m_impl->workers)
        if (t.joinable())
            t.join();
    if (m_impl->monitor.joinable())
        m_impl->monitor.join();
}

void GrpcClientHandler::setResultSink(ResultSink sink)
{
    m_impl->sink = std::move(sink);
}

void GrpcClientHandler::egress(std::uint32_t ticket,
                               std::string model,
                               std::string message,
                               std::string systemPrompt)
{
    {
        std::lock_guard<std::mutex> lock(m_impl->taskMutex);
        m_impl->tasks.push_back(
            {ticket, std::move(model), std::move(message), std::move(systemPrompt)});
    }
    m_impl->taskCv.notify_one();
}

void GrpcClientHandler::poll()
{
    // Idle fast path: a cheap peek, no work when no worker has completed a turn since the last
    // tick. Only when the queue is non-empty do we take the delivery path.
    {
        std::lock_guard<std::mutex> lock(m_impl->doneMutex);
        if (m_impl->done.empty())
            return;
    }
    drain();
}

void GrpcClientHandler::drain()
{
    // Swap the queue out under the lock, then run the sink outside it: the sink writes the
    // ServiceManager on this (main loop) thread, and holding the worker lock across it would
    // serialise workers against the loop for no reason.
    std::deque<std::pair<std::uint32_t, std::string>> ready;
    {
        std::lock_guard<std::mutex> lock(m_impl->doneMutex);
        ready.swap(m_impl->done);
    }
    if (ready.empty() || !m_impl->sink)
        return;
    for (auto& [ticket, doc] : ready)
        m_impl->sink(ticket, std::move(doc));
}

}
