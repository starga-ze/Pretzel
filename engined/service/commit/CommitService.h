#pragma once

#include "ipc/IpcMessage.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pz::engined
{

class CommitEvent;
class CommitAction;
class EnginedServiceManager;

class CommitService
{
public:
    enum class TaskStatus { Pending, Running, Done, Failed };

    struct Task
    {
        int                      id{0};
        std::vector<uint8_t>     payload;
        TaskStatus               status{TaskStatus::Pending};
    };

    CommitService() = default;
    ~CommitService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const CommitEvent& event);
    void handleAction(EnginedServiceManager& serviceManager, const CommitAction& action);

private:
    std::deque<Task> m_queue;
    int              m_nextId{1};

    void startNext(EnginedServiceManager& serviceManager);
    void sendQueueStatus(EnginedServiceManager& serviceManager) const;

    static const char* statusStr(TaskStatus s);
};

} // namespace pz::engined
