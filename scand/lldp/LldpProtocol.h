#pragma once

#include "lldp/LldpTypes.h"

#include <vector>

namespace pz::scand
{

class LldpProtocol
{
public:
    static std::vector<LldpNeighbor> walkNeighbors(void* sessp);
};

}
