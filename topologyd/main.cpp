#include "core/TopologydCore.h"

#include <memory>

using namespace pz::topologyd;

int main()
{
    auto core = std::make_unique<TopologydCore>();

    core->run();

    return 0;
}
