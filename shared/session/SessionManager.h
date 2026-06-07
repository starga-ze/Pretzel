#pragma once

#include "ipc/IpcMessage.h"

namespace pz::session
{

class SessionManager
{
public:
    SessionManager() = default;
    virtual ~SessionManager() = default;

    virtual void handleMessage(const pz::ipc::IpcMessage& msg) = 0;
};

} // namespace pz::session
