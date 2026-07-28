#include "core/ProbedCore.h"

#include <memory>

using namespace pz::probed;

int main()
{
    auto core = std::make_unique<ProbedCore>();

    core->run();

    return 0;
}
