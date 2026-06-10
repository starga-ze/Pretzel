#pragma once

#include "action/SnmpdAction.h"

#include <cstdint>
#include <string>

namespace pz::snmpd
{

enum class ScanActionType : std::uint32_t
{
    Unknown        = 0,
    SendSnmpResult = 1,
};

class ScanAction final : public SnmpdAction
{
public:
    ScanAction(ScanActionType type, std::string resultJson);

    ScanActionType type() const;
    const std::string& resultJson() const;

    void dispatch(SnmpdServiceManager& serviceManager) override;

private:
    ScanActionType m_type{ScanActionType::Unknown};
    std::string    m_resultJson;
};

} // namespace pz::snmpd
