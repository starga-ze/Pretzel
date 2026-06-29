#pragma once

#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpTypes.h"
#include "io/Epoll.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

// Forward-declare net-snmp types to keep this header free of net-snmp macros.
struct snmp_session;
struct snmp_pdu;

namespace pz::scand
{

class SnmpEngine final
{
public:
    SnmpEngine();
    ~SnmpEngine();

    SnmpEngine(const SnmpEngine&) = delete;
    SnmpEngine& operator=(const SnmpEngine&) = delete;

    // Call once at startup — initialises net-snmp, epoll and the v3 worker pool.
    bool init();

    // Kick off a sweep. Each host is probed with v2c on the async epoll path;
    // a v2c timeout is handed to the v3 worker pool for an authPriv fetch.
    // No-op if a scan is already in flight.
    void startScan(std::vector<std::string> ips, SnmpScanConfig cfg);

    // Drive the v2c epoll path and drain v3 worker results. Called every tick.
    bool poll(int timeoutMs);

    bool isScanning() const;

    SnmpEngineHandler* handler();

private:
    // ── v2c async (epoll) path ───────────────────────────────────────────────
    struct SnmpSession
    {
        void*      handle{nullptr};   // snmp_sess_open() opaque handle
        int        fd{-1};            // transport socket fd (in epoll)
        SnmpDevice device;            // accumulates parsed fields
        bool       responded{false};  // got a valid response
        bool       done{false};       // callback fired; reap pending
    };

    bool sendV2c(const std::string& ip);
    void reapDoneSessions();
    static int snmpCallback(int op, snmp_session* sp, int reqId,
                            snmp_pdu* pdu, void* magic);

    // ── blocking worker-pool path (v3 SNMP only) ──────────────────────────────
    // net-snmp performs v3 engineID discovery synchronously on the first send
    // (proven: blocks timeout*(retries+1) regardless of DONT_PROBE / sec level), so
    // it runs on worker threads, never on the main loop. A job is just the IP to
    // retry over v3 once its v2c probe has timed out.
    struct WorkResult
    {
        SnmpDevice device;
        bool       responded{false};
    };

    void       startV3Workers();
    void       stopV3Workers();
    void       v3WorkerLoop();                       // worker thread body
    SnmpDevice probeV3Blocking(const std::string& ip); // runs in a worker thread
    void       enqueueWork(std::string ip);           // main thread
    void       drainV3Results();                     // main thread
    void       checkScanComplete();                  // main thread

    static constexpr int kMaxEvents     = 64;
    static constexpr int kV3WorkerCount = 32;

    bool m_initialized{false};
    bool m_scanActive{false};
    int  m_pendingCount{0};   // active v2c epoll sessions   (main thread only)
    int  m_v3Pending{0};      // v3 work not yet drained back (main thread only)

    SnmpScanConfig m_cfg;     // config for the in-flight sweep (read by workers)

    pz::io::Epoll            m_epoll;
    std::vector<epoll_event> m_events;
    std::unordered_map<int, std::unique_ptr<SnmpSession>> m_sessions;
    std::vector<SnmpDevice>  m_completed;

    // worker pool + thread-safe queues
    std::vector<std::thread> m_workers;
    std::queue<std::string>  m_v3Queue;
    std::mutex               m_v3QueueMu;
    std::condition_variable  m_v3QueueCv;
    std::queue<WorkResult>   m_v3Results;
    std::mutex               m_v3ResultsMu;
    std::atomic<bool>        m_stop{false};

    std::unique_ptr<SnmpEngineHandler> m_handler;
};

} // namespace pz::scand
