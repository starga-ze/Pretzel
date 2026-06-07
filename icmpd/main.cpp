#include "core/IcmpdCore.h"

#include <memory>

using namespace pz::icmpd;

int main()
{
    auto core = std::make_unique<IcmpdCore>();

    core->run();

    return 0;
}
