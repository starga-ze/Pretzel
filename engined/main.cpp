#include "core/EnginedCore.h"

#include <memory>

using namespace nf::engined;

int main()
{
    auto core = std::make_unique<EnginedCore>();

    core->run();

    return 0;
}
