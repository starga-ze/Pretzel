#pragma once

#include "api/ApiEngineHandler.h"
#include "api/ApiTypes.h"
#include "api/VendorApiRegistry.h"
#include "snmp/SnmpTypes.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pz::scand
{

// Runs the vendor-API scan method on its own worker pool, independent of
// SnmpEngine. A device lands here if and only if its scan method is "api" in the
// GUI — there is no SNMP fallback/escalation into this engine: each device is on
// exactly one engine's worklist, decided up front by ScanService, so unlike the
// old v2c -> v3 -> API chain there is nothing here to gate on "did SNMP already
// get enough topology data".
class ApiEngine final
{
public:
    ApiEngine();
    ~ApiEngine();

    ApiEngine(const ApiEngine&) = delete;
    ApiEngine& operator=(const ApiEngine&) = delete;

    // Call once at startup — spins up the worker pool.
    bool init();

    // Kick off a sweep over `devices` (ip -> vendor credential). No-op if a scan
    // is already in flight.
    void startScan(std::map<std::string, ApiCredential> devices);

    // Drain completed worker results. Called every tick.
    bool poll(int timeoutMs);

    bool isScanning() const;

    ApiEngineHandler* handler();

private:
    struct WorkResult
    {
        SnmpDevice device;
        bool       responded{false};
    };

    void startWorkers();
    void stopWorkers();
    void workerLoop();           // worker thread body
    void drainResults();         // main thread
    void checkScanComplete();    // main thread

    static constexpr int kWorkerCount = 16;

    bool m_initialized{false};
    bool m_scanActive{false};
    int  m_pending{0};   // jobs not yet drained back (main thread only)

    std::vector<SnmpDevice> m_completed;

    // worker pool + thread-safe queues
    std::vector<std::thread> m_workers;
    std::queue<std::pair<std::string, ApiCredential>> m_queue;
    std::mutex               m_queueMu;
    std::condition_variable  m_queueCv;
    std::queue<WorkResult>   m_results;
    std::mutex               m_resultsMu;
    std::atomic<bool>        m_stop{false};

    // Vendor providers (PAN-OS, ...). collect() is called from worker threads;
    // providers are stateless / thread-safe across concurrent calls.
    VendorApiRegistry m_apiRegistry;

    std::unique_ptr<ApiEngineHandler> m_handler;
};

} // namespace pz::scand
