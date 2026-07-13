#include "core/ApidCore.h"

#include <memory>

using namespace pz::apid;

int main()
{
    auto core = std::make_unique<ApidCore>();

    core->run();

    return 0;
}
