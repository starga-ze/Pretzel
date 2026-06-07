#include "core/AuthdCore.h"

#include <memory>

using namespace pz::authd;

int main()
{
    auto core = std::make_unique<AuthdCore>();

    core->run();

    return 0;
}
