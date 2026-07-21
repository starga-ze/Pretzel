#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/ScandServiceManager.h"

#include <boost/asio/io_context.hpp>

#include <chrono>

namespace pz::scand
{

class ScandProcess : public pz::process::Process
{
public:
    ScandProcess(pz::ipc::IpcClient* ipcClient, ScandServiceManager* serviceManager,
                 boost::asio::io_context* ioContext);
    ~ScandProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    ScandServiceManager* m_serviceManager;
    boost::asio::io_context* m_ioContext;
};

}
