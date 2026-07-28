#include "core/CollectordCore.h"

#include <memory>

using namespace pz::collectord;

int main()
{
    auto core = std::make_unique<CollectordCore>();

    core->run();

    return 0;
}
