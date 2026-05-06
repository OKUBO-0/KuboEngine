#pragma once

#include <cstdint>
#include <string>

namespace Engine::Graphics3D {
class Model;
class Object3D;
}

namespace DirectXGame {

using ModelHandle = uint32_t;

class GameModelCache {
public:
	static ModelHandle Load(const std::string& modelName);
	static Engine::Graphics3D::Model* Get(ModelHandle handle);
	static const std::string& GetResolvedFileName(ModelHandle handle);
	static void ApplyToObject(Engine::Graphics3D::Object3D& object, ModelHandle handle);
};

}
