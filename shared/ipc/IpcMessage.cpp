#include "ipc/IpcMessage.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace nf::ipc
{

IpcMessage::IpcMessage()
    : m_header(),
      m_payload()
{
}

IpcMessage::IpcMessage(const IpcHeader& header,
                       std::vector<std::uint8_t> payload)
    : m_header(header),
      m_payload(std::move(payload))
{
}

IpcMessage::IpcMessage(IpcHeader&& header,
                       std::vector<std::uint8_t> payload)
    : m_header(std::move(header)),
      m_payload(std::move(payload))
{
}

std::uint8_t IpcMessage::getVersion() const
{
    return m_header.version;
}

void IpcMessage::setVersion(std::uint8_t version)
{
    m_header.version = version;
}

IpcDaemon IpcMessage::getSrc() const
{
    return m_header.src;
}

void IpcMessage::setSrc(IpcDaemon src)
{
    m_header.src = src;
}

IpcDaemon IpcMessage::getDst() const
{
    return m_header.dst;
}

void IpcMessage::setDst(IpcDaemon dst)
{
    m_header.dst = dst;
}

std::uint8_t IpcMessage::getFlags() const
{
    return m_header.flags;
}

void IpcMessage::setFlags(std::uint8_t flags)
{
    m_header.flags = flags;
}

IpcCmd IpcMessage::getCmd() const
{
    return m_header.cmd;
}

void IpcMessage::setCmd(IpcCmd cmd)
{
    m_header.cmd = cmd;
}

std::uint32_t IpcMessage::getSeqNo() const
{
    return m_header.seqNo;
}

void IpcMessage::setSeqNo(std::uint32_t seqNo)
{
    m_header.seqNo = seqNo;
}

const IpcHeader& IpcMessage::getHeader() const
{
    return m_header;
}

IpcHeader& IpcMessage::getHeader()
{
    return m_header;
}

const std::vector<std::uint8_t>& IpcMessage::getPayload() const
{
    return m_payload;
}

std::vector<std::uint8_t>& IpcMessage::getPayload()
{
    return m_payload;
}

void IpcMessage::setPayload(const std::vector<std::uint8_t>& payload)
{
    m_payload = payload;
}

void IpcMessage::setPayload(std::vector<std::uint8_t>&& payload)
{
    m_payload = std::move(payload);
}

void IpcMessage::setPayload(const void* data, std::size_t len)
{
    if (data == nullptr && len != 0)
        throw std::invalid_argument("IpcMessage::setPayload null data with non-zero length");

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_payload.assign(bytes, bytes + len);
}

std::size_t IpcMessage::getPayloadLen() const
{
    return m_payload.size();
}

bool IpcMessage::empty() const
{
    return m_payload.empty();
}

bool IpcMessage::isRequest() const
{
    return m_header.isRequest();
}

bool IpcMessage::isResponse() const
{
    return m_header.isResponse();
}

bool IpcMessage::isError() const
{
    return m_header.isError();
}

bool IpcMessage::isBroadcast() const
{
    return m_header.isBroadcast();
}

IpcWireHeader IpcMessage::toWireHeader() const
{
    IpcWireHeader wire {};
    wire.version = m_header.version;
    wire.src = static_cast<std::uint8_t>(m_header.src);
    wire.dst = static_cast<std::uint8_t>(m_header.dst);
    wire.flags = m_header.flags;
    wire.cmd = static_cast<std::uint16_t>(m_header.cmd);
    wire.reserved = 0;
    wire.seqNo = m_header.seqNo;
    wire.payloadLen = static_cast<std::uint32_t>(m_payload.size());
    return wire;
}

std::string IpcMessage::dump() const
{
    std::ostringstream oss;

    oss << "Version   : " << static_cast<int>(m_header.version) << "\n";
    oss << "Src       : " << IpcProtocol::daemonToStr(m_header.src)
        << " (" << static_cast<int>(m_header.src) << ")\n";
    oss << "Dst       : " << IpcProtocol::daemonToStr(m_header.dst)
        << " (" << static_cast<int>(m_header.dst) << ")\n";
    oss << "Cmd       : " << IpcProtocol::cmdToStr(m_header.cmd)
        << " (" << static_cast<int>(m_header.cmd) << ")\n";
    oss << "Flags     : " << IpcProtocol::flagsToStr(m_header.flags)
        << " (0x"
        << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(m_header.flags)
        << std::dec << ")\n";
    oss << "SeqNo     : " << m_header.seqNo << "\n";
    oss << "Payload   : " << m_payload.size() << " bytes";

    if (!m_payload.empty())
    {
        oss << "\n  ";
        for (std::size_t i = 0; i < m_payload.size(); ++i)
        {
            oss << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(m_payload[i]);

            if (i + 1 != m_payload.size())
                oss << ' ';
        }
        oss << std::dec;
    }

    return oss.str();
}

} // namespace nf::ipc
