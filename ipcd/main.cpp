#include "core/IpcdCore.h"

#include <memory>

using namespace nf::ipcd;

int main() 
{
    auto core = std::make_unique<IpcdCore>();

    core->run();

    return 0;
}
