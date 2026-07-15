#pragma once

#include <cstdint>
#include <string>

namespace pz::config
{

struct LoggerConfig
{
    std::string name;
    std::string file;

    uint32_t maxFileSize{0};
    uint32_t maxFiles{0};
};

struct IpcConfig
{
    std::string socketPath;

    uint32_t maxConnections{128};
    uint32_t maxFrameSize{1048576};

    uint32_t rxBufferSize{1048576};
    uint32_t txBufferSize{1048576};
};

}
