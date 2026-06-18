#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include <memory>

namespace DirectXGame {

class PlayerView final {
public:
	void Initialize();
	void Update();
	void Draw(bool playerVisible, bool aimIndicatorVisible);
	void DrawShadow(bool playerVisible);

	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb(const Vector3& fallbackPosition) const;
	Engine::Math::OBB GetCollisionObb(const Vector3& fallbackPosition) const;

	Engine::Graphics3D::Object3D* GetPlayerObject()
	{
		return playerObject_.get();
	}
	Engine::Graphics3D::Object3D* GetAimIndicatorObject()
	{
		return aimIndicatorObject_.get();
	}

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> playerObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> aimIndicatorObject_;
};

} // namespace DirectXGame
