#pragma once

#include "process/Process.h"

#include "icmp/IcmpEngine.h"
#include "ipc/IpcClient.h"
#include "service/ProbedServiceManager.h"

#include <boost/asio/io_context.hpp>

#include <chrono>

namespace pz::probed
{

class ProbedProcess : public pz::process::Process
{
public:
    ProbedProcess(pz::ipc::IpcClient* ipcClientEngine, IcmpEngine* icmpEngine, ProbedServiceManager* serviceManager,
                  boost::asio::io_context* ioContext);
    ~ProbedProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClientEngine;
    IcmpEngine* m_icmpEngine;
    ProbedServiceManager* m_serviceManager;
    boost::asio::io_context* m_ioContext;
};

}
