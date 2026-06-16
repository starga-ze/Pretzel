#include "core/ScandCore.h"

#include <memory>

using namespace pz::scand;

int main()
{
    auto core = std::make_unique<ScandCore>();

    core->run();

    return 0;
}
