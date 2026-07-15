#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#undef LOG_EMERG
#undef LOG_ALERT
#undef LOG_CRIT
#undef LOG_ERR
#undef LOG_WARNING
#undef LOG_NOTICE
#undef LOG_INFO
#undef LOG_DEBUG

#include "lldp/LldpProtocol.h"
#include "snmp/SnmpEngine.h"
#include "snmp/SnmpProtocol.h"
#include "util/Logger.h"

#include <fcntl.h>
#include <sys/epoll.h>

namespace pz::scand
{

SnmpEngine::SnmpEngine() : m_events(kMaxEvents), m_handler(std::make_unique<SnmpEngineHandler>(this))
{
}

SnmpEngine::~SnmpEngine()
{
    stopV3Workers();
}

bool SnmpEngine::init()
{
    if (m_initialized)
        return true;

    snmp_set_do_debugging(0);
    netsnmp_register_loghandler(NETSNMP_LOGHANDLER_NONE, 0);
    init_snmp("pz-scand");

    if (!m_epoll.init())
    {
        LOG_ERROR("epoll init failed");
        return false;
    }

    startV3Workers();

    m_initialized = true;
    LOG_INFO("net-snmp + epoll + v3 workers initialized (count={})", kV3WorkerCount);
    return true;
}

bool SnmpEngine::isScanning() const
{
    return m_scanActive;
}

SnmpEngineHandler* SnmpEngine::handler()
{
    return m_handler.get();
}

void SnmpEngine::startScan(std::vector<std::string> ips, SnmpScanConfig cfg)
{
    if (m_scanActive)
    {
        LOG_WARN("scan already in flight, ignoring new request");
        return;
    }

    m_cfg = std::move(cfg);
    m_completed.clear();
    m_sessions.clear();
    m_pendingCount = 0;
    m_v3Pending = 0;
    m_scanActive = true;

    LOG_DEBUG("starting scan (ips={}, community={}, port={}, "
              "v2c_probe_ms={}, v3_devices={})",
              ips.size(), m_cfg.community, m_cfg.port, m_cfg.v2cProbeTimeoutMs, m_cfg.v3PerIp.size());

    for (const auto& ip : ips)
    {
        if (!m_cfg.isRegistered(ip))
        {
            LOG_TRACE("no v2c/v3 config — skipping, ICMP only (ip={})", ip);
            continue;
        }

        if (sendV2c(ip))
            ++m_pendingCount;
    }

    checkScanComplete();
}

bool SnmpEngine::sendV2c(const std::string& ip)
{
    netsnmp_session sess;
    snmp_sess_init(&sess);

    const std::string peer = (m_cfg.port != 161) ? ip + ":" + std::to_string(m_cfg.port) : ip;

    auto session = std::make_unique<SnmpSession>();
    session->device.ip = ip;

    SnmpProtocol::configureV2c(sess, peer, m_cfg.communityFor(ip), m_cfg);

    sess.callback = &SnmpEngine::snmpCallback;
    sess.callback_magic = session.get();

    void* handle = snmp_sess_open(&sess);
    if (!handle)
    {
        LOG_TRACE("v2c snmp_sess_open failed (ip={})", ip);
        return false;
    }

    netsnmp_transport* transport = snmp_sess_transport(handle);
    if (!transport || transport->sock < 0)
    {
        LOG_TRACE("no transport fd (ip={})", ip);
        snmp_sess_close(handle);
        return false;
    }

    const int fd = transport->sock;
    fcntl(fd, F_SETFL, O_NONBLOCK);

    if (!m_epoll.add(fd, EPOLLIN | EPOLLET))
    {
        LOG_TRACE("epoll add failed (fd={}, ip={})", fd, ip);
        snmp_sess_close(handle);
        return false;
    }

    netsnmp_pdu* pdu = SnmpProtocol::buildSysGroupGet();
    if (snmp_sess_send(handle, pdu) == 0)
    {
        LOG_TRACE("v2c snmp_sess_send failed (ip={})", ip);
        snmp_free_pdu(pdu);
        m_epoll.del(fd);
        snmp_sess_close(handle);
        return false;
    }

    session->handle = handle;
    session->fd = fd;
    m_sessions.emplace(fd, std::move(session));
    return true;
}

bool SnmpEngine::poll(int timeoutMs)
{
    const int n = m_epoll.wait(m_events, timeoutMs);

    if (n < 0)
    {
        if (errno == EINTR)
            return true;
        LOG_WARN("epoll_wait failed (errno={})", errno);
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        const int fd = m_events[i].data.fd;

        auto it = m_sessions.find(fd);
        if (it == m_sessions.end())
            continue;

        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(fd, &readFds);

        snmp_sess_read(it->second->handle, &readFds);
    }

    for (auto& [fd, session] : m_sessions)
    {
        if (!session->done)
            snmp_sess_timeout(session->handle);
    }

    reapDoneSessions();
    drainV3Results();
    checkScanComplete();

    return true;
}

void SnmpEngine::reapDoneSessions()
{
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        SnmpSession* s = it->second.get();
        if (!s->done)
        {
            ++it;
            continue;
        }

        const SnmpV3Config* v3 = m_cfg.v3For(s->device.ip);
        const bool v3Fallback = !s->responded && v3 != nullptr && !v3->user.empty();

        if (s->responded)
        {
            SnmpProtocol::walkIfPhysAddr(s->handle, s->device);
            SnmpProtocol::walkInterfaceAddrs(s->handle, s->device);
            SnmpProtocol::walkIfTable(s->handle, s->device);
            s->device.lldpNeighbors = LldpProtocol::walkNeighbors(s->handle);
            SnmpProtocol::walkArpTable(s->handle, s->device);
        }

        m_epoll.del(s->fd);
        snmp_sess_close(s->handle);

        if (s->responded)
        {
            m_completed.push_back(std::move(s->device));
        }
        else if (v3Fallback)
        {
            enqueueWork(s->device.ip);
        }

        --m_pendingCount;
        it = m_sessions.erase(it);
    }
}

int SnmpEngine::snmpCallback(int op, snmp_session*, int, snmp_pdu* pdu, void* magic)
{
    auto* session = static_cast<SnmpSession*>(magic);

    if (op == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE && pdu != nullptr && pdu->errstat == SNMP_ERR_NOERROR)
    {
        SnmpProtocol::parseSysGroup(pdu, session->device);
        session->responded = true;

        LOG_TRACE("v2c response (ip={}, sys_name={})", session->device.ip, session->device.sysName);
    }

    session->done = true;
    return 1;
}

void SnmpEngine::startV3Workers()
{
    m_stop = false;
    m_workers.reserve(kV3WorkerCount);
    for (int i = 0; i < kV3WorkerCount; ++i)
        m_workers.emplace_back([this] { v3WorkerLoop(); });
}

void SnmpEngine::stopV3Workers()
{
    {
        std::lock_guard<std::mutex> lk(m_v3QueueMu);
        m_stop = true;
    }
    m_v3QueueCv.notify_all();

    for (auto& t : m_workers)
        if (t.joinable())
            t.join();
    m_workers.clear();
}

void SnmpEngine::v3WorkerLoop()
{
    for (;;)
    {
        std::string ip;
        {
            std::unique_lock<std::mutex> lk(m_v3QueueMu);
            m_v3QueueCv.wait(lk, [this] { return m_stop || !m_v3Queue.empty(); });

            if (m_stop && m_v3Queue.empty())
                return;
            if (m_v3Queue.empty())
                continue;

            ip = std::move(m_v3Queue.front());
            m_v3Queue.pop();
        }

        SnmpDevice dev = probeV3Blocking(ip);
        const bool responded = !dev.sysName.empty() || !dev.sysDescr.empty();

        {
            std::lock_guard<std::mutex> lk(m_v3ResultsMu);
            m_v3Results.push(WorkResult{std::move(dev), responded});
        }
    }
}

SnmpDevice SnmpEngine::probeV3Blocking(const std::string& ip)
{
    SnmpDevice dev;
    dev.ip = ip;

    netsnmp_session sess;
    snmp_sess_init(&sess);

    const std::string peer = (m_cfg.port != 161) ? ip + ":" + std::to_string(m_cfg.port) : ip;

    const SnmpV3Config* v3 = m_cfg.v3For(ip);
    if (!v3 || !SnmpProtocol::configureV3(sess, peer, m_cfg, *v3))
        return dev;

    void* handle = snmp_sess_open(&sess);
    if (!handle)
    {
        LOG_TRACE("v3 snmp_sess_open failed (ip={})", ip);
        return dev;
    }

    netsnmp_pdu* pdu = SnmpProtocol::buildSysGroupGet();
    netsnmp_pdu* resp = nullptr;

    const int status = snmp_sess_synch_response(handle, pdu, &resp);

    if (status == STAT_SUCCESS && resp != nullptr && resp->errstat == SNMP_ERR_NOERROR)
    {
        SnmpProtocol::parseSysGroup(resp, dev);

        SnmpProtocol::walkIfPhysAddr(handle, dev);
        SnmpProtocol::walkInterfaceAddrs(handle, dev);
        SnmpProtocol::walkIfTable(handle, dev);
        dev.lldpNeighbors = LldpProtocol::walkNeighbors(handle);
        SnmpProtocol::walkArpTable(handle, dev);

        LOG_TRACE("v3 response (ip={}, sys_name={}, macs={}, ifs={})", ip, dev.sysName, dev.interfaceMacs.size(),
                  dev.interfaces.size());

        if (!dev.interfaceMacs.empty())
        {
            std::string joined;
            for (size_t i = 0; i < dev.interfaceMacs.size(); ++i)
            {
                if (i)
                    joined += ',';
                joined += dev.interfaceMacs[i];
            }
            LOG_TRACE("mac-fingerprint (ip={}, macs=[{}])", ip, joined);
        }
    }
    else
    {
        LOG_TRACE("v3 no response (ip={}, status={})", ip, status);
    }

    if (resp != nullptr)
        snmp_free_pdu(resp);
    snmp_sess_close(handle);

    return dev;
}

void SnmpEngine::enqueueWork(std::string ip)
{
    const std::string ipCopy = ip;
    {
        std::lock_guard<std::mutex> lk(m_v3QueueMu);
        m_v3Queue.push(std::move(ip));
    }
    ++m_v3Pending;
    m_v3QueueCv.notify_one();
    LOG_TRACE("queued v3 retry (ip={}, pending={})", ipCopy, m_v3Pending);
}

void SnmpEngine::drainV3Results()
{
    std::queue<WorkResult> local;
    {
        std::lock_guard<std::mutex> lk(m_v3ResultsMu);
        std::swap(local, m_v3Results);
    }

    while (!local.empty())
    {
        WorkResult& r = local.front();
        if (r.responded)
            m_completed.push_back(std::move(r.device));
        --m_v3Pending;
        local.pop();
    }
}

void SnmpEngine::checkScanComplete()
{
    if (!m_scanActive)
        return;
    if (m_pendingCount > 0 || m_v3Pending > 0)
        return;

    m_scanActive = false;

    LOG_DEBUG("scan complete (responding={})", m_completed.size());
    auto results = std::move(m_completed);
    m_completed.clear();

    if (m_handler)
        m_handler->onScanComplete(std::move(results));
}

}
