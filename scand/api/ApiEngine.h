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

class ApiEngine final
{
public:
    ApiEngine();
    ~ApiEngine();

    ApiEngine(const ApiEngine&) = delete;
    ApiEngine& operator=(const ApiEngine&) = delete;

    bool init();

    void startScan(std::map<std::string, ApiCredential> devices);

    bool poll(int timeoutMs);

    bool isScanning() const;

    ApiEngineHandler* handler();

private:
    struct WorkResult
    {
        SnmpDevice device;
        bool responded{false};
    };

    void startWorkers();
    void stopWorkers();
    void workerLoop();
    void drainResults();
    void checkScanComplete();

    static constexpr int kWorkerCount = 16;

    bool m_initialized{false};
    bool m_scanActive{false};
    int m_pending{0};

    std::vector<SnmpDevice> m_completed;

    std::vector<std::thread> m_workers;
    std::queue<std::pair<std::string, ApiCredential>> m_queue;
    std::mutex m_queueMu;
    std::condition_variable m_queueCv;
    std::queue<WorkResult> m_results;
    std::mutex m_resultsMu;
    std::atomic<bool> m_stop{false};

    VendorApiRegistry m_apiRegistry;

    std::unique_ptr<ApiEngineHandler> m_handler;
};

}
