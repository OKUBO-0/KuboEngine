#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Graphics3D {
class Model;
class Object3D;
}

namespace DirectXGame {

using ModelHandle = uint32_t;

class GameModelCache {
public:
	static ModelHandle Load(const std::string& modelName);
	static void LoadBatch(const std::vector<std::string>& modelNames);
	static void RefreshModelPathIndex();
	static Engine::Graphics3D::Model* Get(ModelHandle handle);
	static const std::string& GetResolvedFileName(ModelHandle handle);
	static void ApplyToObject(Engine::Graphics3D::Object3D& object, ModelHandle handle);
};

}
