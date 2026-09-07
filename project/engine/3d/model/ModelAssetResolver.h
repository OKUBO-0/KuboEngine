#pragma once

#include <string>

namespace Engine::Graphics3D {

std::string ResolveModelAssetPath(const std::string& directoryPath, const std::string& filename);
std::string ResolveModelResourcePath(const std::string& directoryPath, const std::string& filename);

}
