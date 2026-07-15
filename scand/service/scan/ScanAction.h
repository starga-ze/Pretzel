#pragma once

#include "action/ScandAction.h"

#include <cstdint>
#include <string>

namespace pz::scand
{

enum class ScanActionType : std::uint32_t
{
    Unknown = 0,
    SendScanResult = 1,
};

class ScanAction final : public ScandAction
{
public:
    ScanAction(ScanActionType type, std::string resultJson);

    ScanActionType type() const;
    const std::string& resultJson() const;

    void dispatch(ScandServiceManager& serviceManager) override;

private:
    ScanActionType m_type{ScanActionType::Unknown};
    std::string m_resultJson;
};

}
