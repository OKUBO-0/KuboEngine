#pragma once

#include "Object3D.h"
#include "UILayoutIO.h"
#include "Vector3.h"
#include "Vector4.h"
#include <cstdint>
#include <memory>
#include <vector>

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
	void DrawFloatingShadow(bool visible);
	void DrawModel(bool visible);
	void DrawShadow(bool visible);
	Engine::Graphics3D::Object3D* GetObject() const
	{
		return simplifiedRenderEnabled_ && simplifiedObject_
			? simplifiedObject_.get()
			: object_.get();
	}
	void SetSimplifiedRenderEnabled(bool enabled);
	bool IsSimplifiedRenderEnabled() const { return simplifiedRenderEnabled_; }
	void ApplyDeathPose(
		const Vector3& position,
		float rotationY,
		float progress);
	void ApplyPresentationPose(
		const Vector3& position,
		float rotationY,
		bool idleMotion);
	void NotifyAttack();
	void NotifyJump();
	void NotifyLand();
	void SetAnimationUpdateStride(uint32_t stride);
	void SetFrustumCullingEnabled(bool enabled);

	void SetModelByType(int32_t type);
	void SetFloatingEnabled(bool enabled);
	void SetBehaviorVisual(
		const Vector4& color,
		float scaleMultiplier = 1.0f);
	void ClearBehaviorVisual();
	void SetSpawnScaleMultiplier(float scaleMultiplier)
	{
		spawnScaleMultiplier_ = scaleMultiplier;
	}

	static void LoadVisualTuning(
		const DirectXGame::UILayoutIO::LayoutMap& tuning);
	static void AppendVisualTuningEntries(
		std::vector<DirectXGame::UILayoutIO::Entry>& entries);
	static float GetOctopusModelGroundOffsetY();
	static void SetOctopusModelGroundOffsetY(float offsetY);

	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb(
		const Vector3& fallbackPosition) const;
	Engine::Math::OBB GetCollisionObb(
		const Vector3& fallbackPosition) const;

private:
	void InitializeFloatingShadow();
	void InitializeSimplifiedObject();
	void ApplyTransform(
		const Vector3& position,
		float rotationY);
	void ApplySimplifiedTransform(
		const Vector3& position,
		float rotationY);
	float GetEffectiveModelVerticalOffsetY() const;
	void UpdateFloatingShadow(
		const Vector3& position,
		float rotationY);

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	std::unique_ptr<Engine::Graphics3D::Object3D> simplifiedObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> floatingShadowObject_;
	Vector4 behaviorColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float behaviorScaleMultiplier_ = 1.0f;
	float spawnScaleMultiplier_ = 1.0f;
	float modelVisualScaleMultiplier_ = 1.0f;
	float modelVerticalOffsetY_ = 0.0f;
	float animationTime_ = 0.0f;
	float impactMotionTimer_ = 0.0f;
	float attackMotionTimer_ = 0.0f;
	float jumpMotionTimer_ = 0.0f;
	float landMotionTimer_ = 0.0f;
	int32_t enemyType_ = 0;
	bool simplifiedRenderEnabled_ = false;
	bool usesOctopusGroundOffset_ = false;
	bool floatingEnabled_ = false;
};

} // namespace DirectXGame
