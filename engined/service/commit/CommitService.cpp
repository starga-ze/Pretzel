#include "service/commit/CommitService.h"

#include "service/commit/CommitEvent.h"
#include "service/commit/CommitAction.h"
#include "service/EnginedServiceManager.h"

#include "action/EnginedActionFactory.h"
#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::engined
{

using json = nlohmann::json;

// ── helpers ──────────────────────────────────────────────────────────────────

const char* CommitService::statusStr(TaskStatus s)
{
    switch (s)
    {
    case TaskStatus::Pending: return "pending";
    case TaskStatus::Running: return "running";
    case TaskStatus::Done:    return "done";
    case TaskStatus::Failed:  return "failed";
    }
    return "unknown";
}

void CommitService::sendQueueStatus(EnginedServiceManager& serviceManager) const
{
    json arr = json::array();
    for (const auto& t : m_queue)
    {
        arr.push_back({{"id", t.id}, {"status", statusStr(t.status)}});
    }

    const std::string payload = arr.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Engined);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::CommitQueueStatus);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<uint8_t>(payload.begin(), payload.end()));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
    LOG_DEBUG("CommitService: queue snapshot sent — {} task(s)", m_queue.size());
}

void CommitService::startNext(EnginedServiceManager& serviceManager)
{
    // Find the first pending task and promote it to Running.
    for (auto& t : m_queue)
    {
        if (t.status != TaskStatus::Pending)
            continue;

        t.status = TaskStatus::Running;
        sendQueueStatus(serviceManager);

        auto action = serviceManager.actionFactory()->create(
            EnginedActionDomain::Commit,
            static_cast<std::uint32_t>(CommitActionType::ApplyCommit));
        serviceManager.postAction(std::move(action));
        return;
    }
}

// ── event handler ─────────────────────────────────────────────────────────────

void CommitService::handleEvent(EnginedServiceManager& serviceManager,
                                const CommitEvent& event)
{
    switch (event.type())
    {
    case CommitEventType::ReceiveSettingsCommit:
    {
        const pz::ipc::IpcMessage* msg = event.message();
        if (!msg || msg->getPayload().empty())
        {
            LOG_ERROR("CommitService: SettingsCommitRequest payload is empty");
            return;
        }

        Task task;
        task.id      = m_nextId++;
        task.payload = msg->getPayload();
        task.status  = TaskStatus::Pending;
        m_queue.push_back(std::move(task));

        LOG_INFO("CommitService: task {} enqueued — queue size={}", m_queue.back().id, m_queue.size());
        sendQueueStatus(serviceManager);

        // Start immediately if this is the only task (no Running task exists).
        const bool anyRunning = std::any_of(m_queue.begin(), m_queue.end(),
            [](const Task& t) { return t.status == TaskStatus::Running; });

        if (!anyRunning)
            startNext(serviceManager);

        break;
    }

    case CommitEventType::ReloadComplete:
    {
        // Mark the Running task as Done and evict completed/failed tasks from front.
        for (auto& t : m_queue)
        {
            if (t.status == TaskStatus::Running)
            {
                t.status = TaskStatus::Done;
                LOG_INFO("CommitService: task {} done", t.id);
                break;
            }
        }

        // Trim leading done/failed tasks (keep at most last 5 for history).
        while (m_queue.size() > 5 ||
               (!m_queue.empty() &&
                (m_queue.front().status == TaskStatus::Done ||
                 m_queue.front().status == TaskStatus::Failed)))
        {
            m_queue.pop_front();
        }

        sendQueueStatus(serviceManager);
        startNext(serviceManager);
        break;
    }

    default:
        LOG_WARN("CommitService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

// ── action handler ────────────────────────────────────────────────────────────

void CommitService::handleAction(EnginedServiceManager& serviceManager,
                                 const CommitAction& action)
{
    switch (action.type())
    {
    case CommitActionType::ApplyCommit:
    {
        // Find the Running task.
        Task* running = nullptr;
        for (auto& t : m_queue)
        {
            if (t.status == TaskStatus::Running) { running = &t; break; }
        }

        if (!running)
        {
            LOG_ERROR("CommitService: ApplyCommit fired but no Running task found");
            return;
        }

        json changes;
        try
        {
            changes = json::parse(running->payload.begin(), running->payload.end());
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("CommitService: failed to parse commit payload — {}", e.what());
            running->status = TaskStatus::Failed;
            sendQueueStatus(serviceManager);
            startNext(serviceManager);
            return;
        }

        if (!changes.is_array())
        {
            LOG_ERROR("CommitService: payload root must be a JSON array");
            running->status = TaskStatus::Failed;
            sendQueueStatus(serviceManager);
            startNext(serviceManager);
            return;
        }

        int applied = 0;
        int failed  = 0;

        // Build the next config version: start from the current running-config root
        // and overlay each change at <daemon>.<service|system>.<domain>.
        json root = pz::config::Config::runningConfigRoot();

        for (const auto& change : changes)
        {
            const std::string daemon = change.value("daemon", "");
            const std::string domain = change.value("domain", "");

            if (daemon.empty() || domain.empty() || !change.contains("values"))
            {
                LOG_WARN("CommitService: skipping malformed change entry");
                failed++;
                continue;
            }

            const json& values = change["values"];
            if (!values.is_object())
            {
                LOG_WARN("CommitService: 'values' not object daemon={} domain={}", daemon, domain);
                failed++;
                continue;
            }

            // "system" sections (ipc/logger) vs "service" sections (everything else).
            // Place the change where the domain already lives; default to "service".
            const char* parent = "service";
            if (root.contains(daemon) && root[daemon].contains("system") &&
                root[daemon]["system"].contains(domain))
            {
                parent = "system";
            }

            root[daemon][parent][domain].merge_patch(values);
            LOG_INFO("CommitService: staged daemon={} {}.{} keys={}",
                     daemon, parent, domain, values.size());
            applied++;
        }

        if (applied == 0)
        {
            LOG_ERROR("CommitService: no changes staged — task {} failed", running->id);
            running->status = TaskStatus::Failed;
            sendQueueStatus(serviceManager);
            startNext(serviceManager);
            return;
        }

        // Persist as a brand-new running_config version (history append).
        if (!pz::config::Config::commitConfig(root))
        {
            LOG_ERROR("CommitService: commitConfig failed — task {} failed", running->id);
            running->status = TaskStatus::Failed;
            sendQueueStatus(serviceManager);
            startNext(serviceManager);
            return;
        }

        LOG_INFO("CommitService: task {} — {} staged, {} skipped — committed new "
                 "running_config version; scheduling reload",
                 running->id, applied, failed);

        static constexpr pz::ipc::IpcDaemon kServiceDaemons[] = {
            pz::ipc::IpcDaemon::Authd,
            pz::ipc::IpcDaemon::Icmpd,
            pz::ipc::IpcDaemon::Snmpd,
            pz::ipc::IpcDaemon::Topologyd,
        };

        for (const auto dst : kServiceDaemons)
        {
            auto cfgMsg = std::make_unique<pz::ipc::IpcMessage>();
            cfgMsg->setSrc(pz::ipc::IpcDaemon::Engined);
            cfgMsg->setDst(dst);
            cfgMsg->setCmd(pz::ipc::IpcCmd::ConfigReload);
            cfgMsg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
            serviceManager.txRouter().handleIpcMessage(std::move(cfgMsg));
            LOG_INFO("CommitService: ConfigReload sent to {}", pz::ipc::IpcProtocol::daemonToStr(dst));
        }

        pz::config::Config::invalidateConfigCache();
        serviceManager.bootstrapService().scheduleServiceReload();
        // ReloadComplete event will arrive from BootstrapService when all daemons are back up.
        break;
    }

    default:
        LOG_WARN("CommitService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

} // namespace pz::engined
