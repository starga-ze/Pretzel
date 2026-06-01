#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"

#include "event/IcmpdEvent.h"

#include <queue>

namespace nf::icmpd
{

class IcmpdServiceManager : public nf::service::ServiceManager
{
public:
    IcmpdServiceManager();
    ~IcmpdServiceManager() override = default;

    void start() override;

    void schedule() override;
    void dispatch(std::unique_ptr<nf::event::Event> event) override;
    void execute() override;

private:
    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::queue<std::unique_ptr<nf::event::Event>> m_eventQueue;
};

} // namespace nf::icmpd
