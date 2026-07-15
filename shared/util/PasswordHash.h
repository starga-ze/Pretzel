#pragma once

#include <string>

namespace pz::util
{

std::string hashSha256(const std::string& password, const std::string& salt);

std::string generateSalt();

}
