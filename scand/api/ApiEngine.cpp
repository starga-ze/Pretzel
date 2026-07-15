#include "api/ApiEngine.h"
#include "util/Logger.h"

namespace pz::scand
{

ApiEngine::ApiEngine() : m_handler(std::make_unique<ApiEngineHandler>(this))
{
}

ApiEngine::~ApiEngine()
{
    stopWorkers();
}

bool ApiEngine::init()
{
    if (m_initialized)
        return true;

    startWorkers();

    m_initialized = true;
    LOG_INFO("workers initialized (count={})", kWorkerCount);
    return true;
}

bool ApiEngine::isScanning() const
{
    return m_scanActive;
}

ApiEngineHandler* ApiEngine::handler()
{
    return m_handler.get();
}

void ApiEngine::startScan(std::map<std::string, ApiCredential> devices)
{
    if (m_scanActive)
    {
        LOG_WARN("scan already in flight, ignoring new request");
        return;
    }

    m_completed.clear();
    m_pending = 0;
    m_scanActive = true;

    LOG_DEBUG("starting scan (devices={})", devices.size());

    {
        std::lock_guard<std::mutex> lk(m_queueMu);
        for (auto& [ip, cred] : devices)
            m_queue.emplace(ip, std::move(cred));
    }
    m_pending = static_cast<int>(devices.size());
    m_queueCv.notify_all();

    checkScanComplete();
}

bool ApiEngine::poll(int)
{
    drainResults();
    checkScanComplete();
    return true;
}

void ApiEngine::startWorkers()
{
    m_stop = false;
    m_workers.reserve(kWorkerCount);
    for (int i = 0; i < kWorkerCount; ++i)
        m_workers.emplace_back([this] { workerLoop(); });
}

void ApiEngine::stopWorkers()
{
    {
        std::lock_guard<std::mutex> lk(m_queueMu);
        m_stop = true;
    }
    m_queueCv.notify_all();

    for (auto& t : m_workers)
        if (t.joinable())
            t.join();
    m_workers.clear();
}

void ApiEngine::workerLoop()
{
    for (;;)
    {
        std::pair<std::string, ApiCredential> job;
        {
            std::unique_lock<std::mutex> lk(m_queueMu);
            m_queueCv.wait(lk, [this] { return m_stop || !m_queue.empty(); });

            if (m_stop && m_queue.empty())
                return;
            if (m_queue.empty())
                continue;

            job = std::move(m_queue.front());
            m_queue.pop();
        }

        SnmpDevice dev;
        dev.ip = job.first;
        const bool responded = m_apiRegistry.collect(job.second, dev);

        {
            std::lock_guard<std::mutex> lk(m_resultsMu);
            m_results.push(WorkResult{std::move(dev), responded});
        }
    }
}

void ApiEngine::drainResults()
{
    std::queue<WorkResult> local;
    {
        std::lock_guard<std::mutex> lk(m_resultsMu);
        std::swap(local, m_results);
    }

    while (!local.empty())
    {
        WorkResult& r = local.front();
        if (r.responded)
            m_completed.push_back(std::move(r.device));
        --m_pending;
        local.pop();
    }
}

void ApiEngine::checkScanComplete()
{
    if (!m_scanActive)
        return;
    if (m_pending > 0)
        return;

    m_scanActive = false;

    LOG_DEBUG("scan complete (responding={})", m_completed.size());
    auto results = std::move(m_completed);
    m_completed.clear();

    if (m_handler)
        m_handler->onScanComplete(std::move(results));
}

}
