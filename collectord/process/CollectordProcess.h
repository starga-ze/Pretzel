#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/CollectordServiceManager.h"

#include <boost/asio/io_context.hpp>

#include <chrono>

namespace pz::collectord
{

class CollectordProcess : public pz::process::Process
{
public:
    CollectordProcess(pz::ipc::IpcClient* ipcClient, CollectordServiceManager* serviceManager,
                 boost::asio::io_context* ioContext);
    ~CollectordProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    CollectordServiceManager* m_serviceManager;
    boost::asio::io_context* m_ioContext;
};

}
