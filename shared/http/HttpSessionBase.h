#pragma once

#include "http/HttpMessage.h"

namespace pz::http
{

class HttpSessionBase
{
public:
    virtual ~HttpSessionBase() = default;

    virtual void send(HttpResponse response) = 0;

    SessionId id() const
    {
        return m_id;
    }
    void setId(SessionId id)
    {
        m_id = id;
    }

protected:
    SessionId m_id{0};
};

}
