#include "grpc/GrpcClientHandler.h"

#include "grpc/GrpcClient.h"

#include "util/Logger.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
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

std::string jsonEscape(const std::string& raw)
{
    std::string escaped;
    escaped.reserve(raw.size());
    for (char c : raw)
    {
        if (c == '"' || c == '\\')
            escaped += '\\';
        if (c == '\n' || c == '\r' || c == '\t')
            escaped += ' ';
        else
            escaped += c;
    }
    return escaped;
}

// A call whose stream never reached the server's answer (the process is down, the connection
// dropped) still has to resolve whatever the browser is waiting on — so synthesize the document
// shape that call's controller already knows how to read.
std::string unreachableDoc(GrpcCmd cmd, const std::string& reason)
{
    const std::string escaped = jsonEscape(reason);
    if (cmd == GrpcCmd::Chat)
        return R"({"ok":false,"code":"UNREACHABLE","error":")" + escaped + R"("})";
    if (cmd == GrpcCmd::CorpusRefresh)
        return R"({"stage":"failed","final":true,"error":")" + escaped + R"("})";
    return R"({"error":")" + escaped + R"("})";
}

}

struct GrpcClientHandler::Impl
{
    // target is copied for the log line before it is moved into the client.
    explicit Impl(std::string t) : target(t), client(std::move(t)) {}

    std::string target;
    GrpcClient client;

    std::mutex taskMutex;
    std::condition_variable taskCv;
    std::deque<GrpcMessage> tasks;
    std::atomic<bool> stopping{false};

    std::mutex doneMutex;
    std::deque<std::tuple<GrpcCmd, std::uint32_t, std::string>> done;

    ResultSink sink;
    std::vector<std::thread> workers;
    std::thread monitor;

    // Streaming calls get their own thread: one runs for minutes, and letting it occupy a worker
    // would starve the turns the console is waiting on.
    std::thread streamThread;
    std::atomic<bool> streaming{false};

    void report(GrpcCmd cmd, std::uint32_t ticket, std::string json)
    {
        std::lock_guard<std::mutex> lock(doneMutex);
        done.emplace_back(cmd, ticket, std::move(json));
    }

    void runUnary(const GrpcMessage& task)
    {
        std::string error;
        std::string json;

        switch (task.cmd)
        {
        case GrpcCmd::Chat:
        {
            // Deltas are dropped for now — the poll-based console reads only the final document.
            // The callback is where the future browser-side SSE stream will hook in.
            auto outcome = client.chat(task.model, task.message, task.systemPrompt,
                                       [](const std::string&) {});
            json = std::move(outcome.resultJson);
            error = std::move(outcome.error);
            break;
        }
        case GrpcCmd::CorpusCheck:
            json = client.checkCorpus(task.message, error);
            break;
        case GrpcCmd::CorpusStatus:
            json = client.corpusStatus(error);
            break;
        default:
            error = "unroutable command";
            break;
        }

        // Reaching pretzel-ai at all (even to be told the call failed) is routine; not reaching it
        // is the connection-level event worth a warning.
        if (json.empty())
        {
            LOG_WARN("pretzel-ai {} unreachable (ticket={}): {}", grpcCmdToStr(task.cmd),
                     task.ticket, error.empty() ? "no result" : error);
            json = unreachableDoc(task.cmd,
                                  error.empty() ? "pretzel-ai returned no result" : error);
        }
        else
        {
            LOG_DEBUG("pretzel-ai {} answered (ticket={}, {} bytes)", grpcCmdToStr(task.cmd),
                      task.ticket, json.size());
        }

        report(task.cmd, task.ticket, std::move(json));
    }

    void runStream(GrpcMessage task)
    {
        std::string error;
        bool sawFinal = false;

        client.refreshCorpus(task.message,
                             [this, &task, &sawFinal](const std::string& json)
                             {
                                 if (json.find("\"final\":true") != std::string::npos)
                                     sawFinal = true;
                                 report(task.cmd, task.ticket, json);
                             },
                             error);

        // Only synthesize a terminal message when the server never sent one; otherwise the
        // server's own final message is the more accurate account of what happened.
        if (!sawFinal)
        {
            // A cancelled stream also ends without a final message, but it is not a failure — the
            // operator asked for it, and the pages already written stay written. Reported as its
            // own stage so the card can say so instead of showing an error nobody caused.
            const bool cancelled = error.find("CANCELLED") != std::string::npos
                                   || error.find("Cancelled") != std::string::npos;
            if (cancelled)
            {
                LOG_INFO("tech-doc refresh cancelled");
                report(task.cmd, task.ticket,
                       R"({"stage":"cancelled","final":true})");
            }
            else
            {
                LOG_WARN("tech-doc refresh ended without a final message: {}",
                         error.empty() ? "stream closed" : error);
                report(task.cmd, task.ticket,
                       unreachableDoc(task.cmd,
                                      error.empty() ? "the refresh stream closed" : error));
            }
        }
        else
        {
            LOG_INFO("tech-doc refresh finished");
        }

        streaming.store(false);
    }

    void run()
    {
        for (;;)
        {
            GrpcMessage task;
            {
                std::unique_lock<std::mutex> lock(taskMutex);
                taskCv.wait(lock, [this] { return stopping.load() || !tasks.empty(); });
                if (stopping.load() && tasks.empty())
                    return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            runUnary(task);
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
    if (m_impl->streamThread.joinable())
        m_impl->streamThread.join();
}

void GrpcClientHandler::setResultSink(ResultSink sink)
{
    m_impl->sink = std::move(sink);
}

void GrpcClientHandler::egress(GrpcMessage message)
{
    // Cancel does not queue. Every other command waits its turn behind the worker pool, but a
    // cancel that waited would be a cancel that arrives after the thing it was cancelling — and
    // the operator pressed it because they want the crawl to stop now.
    if (message.cmd == GrpcCmd::CorpusCancel)
    {
        LOG_INFO("cancelling the in-flight tech-doc refresh");
        m_impl->client.cancelRefresh();
        return;
    }

    if (grpcCmdStreams(message.cmd))
    {
        // Whether a second refresh is allowed was already decided by the ServiceManager; this
        // guard is only here so a bug there cannot leave two threads crawling at once.
        bool expected = false;
        if (!m_impl->streaming.compare_exchange_strong(expected, true))
        {
            LOG_WARN("refusing a second {} while one is in flight", grpcCmdToStr(message.cmd));
            m_impl->report(message.cmd, message.ticket,
                           unreachableDoc(message.cmd, "a refresh is already running"));
            return;
        }
        // The previous stream's thread has finished its work but may not have been joined yet.
        if (m_impl->streamThread.joinable())
            m_impl->streamThread.join();
        m_impl->streamThread =
            std::thread([this, message = std::move(message)]() mutable
                        { m_impl->runStream(std::move(message)); });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->taskMutex);
        m_impl->tasks.push_back(std::move(message));
    }
    m_impl->taskCv.notify_one();
}

void GrpcClientHandler::poll()
{
    // Idle fast path: a cheap peek, no work when no worker has produced an answer since the last
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
    // Swap the queue out under the lock, then run the sink outside it: the sink posts events on
    // this (main loop) thread, and holding the worker lock across it would serialise workers
    // against the loop for no reason.
    std::deque<std::tuple<GrpcCmd, std::uint32_t, std::string>> ready;
    {
        std::lock_guard<std::mutex> lock(m_impl->doneMutex);
        ready.swap(m_impl->done);
    }
    if (ready.empty() || !m_impl->sink)
        return;
    for (auto& [cmd, ticket, json] : ready)
        m_impl->sink(cmd, ticket, std::move(json));
}

}
