#pragma once

#include "Object3D.h"
#include <memory>

namespace DirectXGame {

class SkyDome {
public:
	void Initialize();
	void Update();
	void Draw();

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> skyObject_;
	float animationTime_ = 0.0f;
};

} // namespace DirectXGame
