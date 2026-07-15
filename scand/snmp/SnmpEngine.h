#pragma once

#include "io/Epoll.h"
#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpTypes.h"

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

    bool init();

    void startScan(std::vector<std::string> ips, SnmpScanConfig cfg);

    bool poll(int timeoutMs);

    bool isScanning() const;

    SnmpEngineHandler* handler();

private:
    struct SnmpSession
    {
        void* handle{nullptr};
        int fd{-1};
        SnmpDevice device;
        bool responded{false};
        bool done{false};
    };

    bool sendV2c(const std::string& ip);
    void reapDoneSessions();
    static int snmpCallback(int op, snmp_session* sp, int reqId, snmp_pdu* pdu, void* magic);

    struct WorkResult
    {
        SnmpDevice device;
        bool responded{false};
    };

    void startV3Workers();
    void stopV3Workers();
    void v3WorkerLoop();
    SnmpDevice probeV3Blocking(const std::string& ip);
    void enqueueWork(std::string ip);
    void drainV3Results();
    void checkScanComplete();

    static constexpr int kMaxEvents = 64;
    static constexpr int kV3WorkerCount = 32;

    bool m_initialized{false};
    bool m_scanActive{false};
    int m_pendingCount{0};
    int m_v3Pending{0};

    SnmpScanConfig m_cfg;

    pz::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;
    std::unordered_map<int, std::unique_ptr<SnmpSession>> m_sessions;
    std::vector<SnmpDevice> m_completed;

    std::vector<std::thread> m_workers;
    std::queue<std::string> m_v3Queue;
    std::mutex m_v3QueueMu;
    std::condition_variable m_v3QueueCv;
    std::queue<WorkResult> m_v3Results;
    std::mutex m_v3ResultsMu;
    std::atomic<bool> m_stop{false};

    std::unique_ptr<SnmpEngineHandler> m_handler;
};

}
