#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "Vector4.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class EnemyView final {
public:
	void Initialize();
	void Update(
		const Vector3& position,
		float rotationY,
		bool hitFlash,
		bool phaseFlash);
	void Draw(bool visible);
	void DrawShadow(bool visible);
	void ApplyDeathPose(
		const Vector3& position,
		float rotationY,
		float progress);

	void SetModelByType(int32_t type);
	void SetFloatingEnabled(bool enabled);
	void SetBehaviorVisual(
		const Vector4& color,
		float scaleMultiplier = 1.0f);
	void ClearBehaviorVisual();

	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb(
		const Vector3& fallbackPosition) const;
	Engine::Math::OBB GetCollisionObb(
		const Vector3& fallbackPosition) const;

private:
	void InitializeFloatingShadow();
	void ApplyTransform(
		const Vector3& position,
		float rotationY);
	void UpdateFloatingShadow(
		const Vector3& position,
		float rotationY);

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	std::unique_ptr<Engine::Graphics3D::Object3D> floatingShadowObject_;
	Vector4 behaviorColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float behaviorScaleMultiplier_ = 1.0f;
	bool floatingEnabled_ = false;
};

} // namespace DirectXGame
