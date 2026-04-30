#pragma once

#include "service/ServiceManager.h"

namespace nf::ipcd
{

class IpcdServiceManager : public nf::service::ServiceManager
{
public:
    explicit IpcdServiceManager() = default;
    ~IpcdServiceManager() override = default;

    void dispatch() override;
};

} // namespace nf::ipcd
