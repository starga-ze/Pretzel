#include "core/IpcdCore.h"

#include <memory>

using namespace pz::ipcd;

int main()
{
    auto core = std::make_unique<IpcdCore>();

    core->run();

    return 0;
}
