#include "core/SnmpdCore.h"

#include <memory>

using namespace nf::snmpd;

int main()
{
    auto core = std::make_unique<SnmpdCore>();

    core->run();

    return 0;
}
