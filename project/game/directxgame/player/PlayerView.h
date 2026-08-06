#pragma once

#include "GameSession.h"
#include "Object3D.h"
#include "Vector3.h"
#include <memory>

namespace DirectXGame {

class PlayerView final {
public:
	void Initialize();
	void SetCharacterId(CharacterId characterId);
	void SetMoving(bool moving);
	void SetMotionState(bool moving, bool dashing, bool jumping);
	void NotifyHitReact();
	void SetDeathAnimationActive(bool active);
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
	CharacterId characterId_ = CharacterId::Default;
	bool moving_ = false;
	bool dashing_ = false;
	bool jumping_ = false;
	float hitReactTimer_ = 0.0f;
	bool deathAnimationActive_ = false;
	std::unique_ptr<Engine::Graphics3D::Object3D> playerObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> aimIndicatorObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> contactShadowObject_;
};

} // namespace DirectXGame
