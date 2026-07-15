#pragma once

#include "api/ApiTypes.h"

#include <map>
#include <string>

namespace pz::scand
{

class ApiService
{
public:
    ApiService() = default;

    std::map<std::string, ApiCredential> loadCredentials() const;
};

}
