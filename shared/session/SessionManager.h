#pragma once

#include "ipc/IpcMessage.h"

namespace nf::session
{

class SessionManager
{
public:
    SessionManager() = default;
    virtual ~SessionManager() = default;

    virtual void handleMessage(const nf::ipc::IpcMessage& msg) = 0;
};

} // namespace nf::session
