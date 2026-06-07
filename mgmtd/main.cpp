#include "core/MgmtdCore.h"

#include <memory>

using namespace pz::mgmtd;

int main()
{
    auto core = std::make_unique<MgmtdCore>();

    core->run();

    return 0;
}
