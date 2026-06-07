#include "icmp/IcmpProtocol.h"

namespace pz::icmpd
{

const char* IcmpProtocol::typeToStr(IcmpType type)
{
    switch (type)
    {
    case IcmpType::EchoReply:
        return "EchoReply";
    case IcmpType::DestinationUnreachable:
        return "DestinationUnreachable";
    case IcmpType::EchoRequest:
        return "EchoRequest";
    case IcmpType::TimeExceeded:
        return "TimeExceeded";
    default:
        return "Unknown";
    }
}

const char* IcmpProtocol::codeToStr(IcmpType type, IcmpCode code)
{
    if ((type == IcmpType::EchoRequest || type == IcmpType::EchoReply) &&
        code == IcmpCode::Echo)
    {
        return "Echo";
    }

    return "Unknown";
}

} // namespace pz::icmpd
