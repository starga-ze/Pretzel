#pragma once

#include <cstdint>

namespace pz::icmpd
{

enum class IcmpType : std::uint8_t
{
    EchoReply              = 0,
    DestinationUnreachable = 3,
    EchoRequest            = 8,
    TimeExceeded           = 11,
};

enum class IcmpCode : std::uint8_t
{
    Echo = 0,
};

class IcmpProtocol final
{
public:
    static const char* typeToStr(IcmpType type);
    static const char* codeToStr(IcmpType type, IcmpCode code);
};

} // namespace pz::icmpd
