#pragma once

#include "Enemy.h"
#include "Vector3.h"
#include <array>
#include <memory>
#include <vector>

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class BossRushTelegraph final {
public:
	BossRushTelegraph();
	~BossRushTelegraph();
	void Update(const BossAttackTelegraph& telegraph);
	void Draw() const;
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;
	Engine::Graphics3D::Object3D* GetRenderObject() const
	{
		return visible_ ? objects_[0].get() : nullptr;
	}

private:
	void EnsureInitialized(size_t count);
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 512> objects_;
	size_t visibleCount_ = 0;
	bool visible_ = false;
};

class BossAreaTelegraph final {
public:
	BossAreaTelegraph();
	~BossAreaTelegraph();
	void Update(const BossAttackTelegraph& telegraph);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;
	void AppendShadowObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;
	bool HasLockedCrystalPositions() const { return crystalPositionsLocked_; }
	const std::array<Vector3, 4>& GetCrystalPositions() const { return crystalPositions_; }

private:
	void EnsureInitialized(size_t count);
	void EnsureCrystals();
	void LockCrystalPositions(const Vector3& targetPosition);
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 768> objects_;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 4> crystalObjects_;
	std::array<Vector3, 4> crystalPositions_{};
	size_t visibleCount_ = 0;
	size_t crystalVisibleCount_ = 0;
	int32_t crystalLingerFrames_ = 0;
	bool crystalPositionsLocked_ = false;
	bool visible_ = false;
};

class BossBeamBurst final {
public:
	BossBeamBurst();
	~BossBeamBurst();
	void Initialize(
		const Vector3& position,
		const Vector3& direction,
		int32_t beamCount,
		float length,
		float width,
		float angleOffset);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized(size_t count);
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 3> objects_;
	Vector3 position_{};
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float length_ = 34.0f;
	float width_ = 2.2f;
	float angleOffset_ = 0.38f;
	size_t visibleCount_ = 0;
	float elapsedTime_ = 0.0f;
	bool active_ = false;
};

class BossBeamExplosionChain final {
public:
	BossBeamExplosionChain();
	~BossBeamExplosionChain();
	void Initialize(
		const Vector3& origin,
		const Vector3& direction,
		int32_t beamCount,
		float length,
		float spacing,
		float interval,
		float radius,
		float startDelay,
		float angleOffset);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized(size_t count);
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 128> objects_;
	Vector3 origin_{};
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float length_ = 34.0f;
	float spacing_ = 7.0f;
	float interval_ = 0.08f;
	float radius_ = 2.8f;
	float startDelay_ = 0.22f;
	float angleOffset_ = 0.38f;
	size_t beamCount_ = 1;
	size_t explosionCountPerBeam_ = 1;
	size_t visibleCount_ = 0;
	float elapsedTime_ = 0.0f;
	bool active_ = false;
};

class BossShockwaveRing final {
public:
	BossShockwaveRing();
	~BossShockwaveRing();
	void Initialize(
		const Vector3& center,
		float radius,
		float delay,
		const Vector4& color,
		float duration = 2.4f,
		float particleSize = 0.5f,
		float particleY = 0.18f,
		bool fixedRadius = false,
		size_t segmentCount = 96,
		bool inward = false);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized();
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 96> objects_;
	Vector3 center_{};
	Vector4 color_{ 1.0f, 0.35f, 0.04f, 0.9f };
	float radius_ = 8.0f;
	float delay_ = 0.0f;
	float duration_ = 2.4f;
	float particleSize_ = 0.5f;
	float particleY_ = 0.18f;
	float elapsedTime_ = 0.0f;
	size_t segmentCount_ = 96;
	bool fixedRadius_ = false;
	bool inward_ = false;
	bool active_ = false;
};

class BossDomeBurstVisual final {
public:
	BossDomeBurstVisual();
	~BossDomeBurstVisual();
	void Initialize(
		const Vector3& center,
		float radius,
		float duration,
		const Vector4& color);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized();
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 512> objects_;
	Vector3 center_{};
	Vector4 color_{ 1.0f, 0.08f, 0.16f, 0.92f };
	float radius_ = 32.0f;
	float duration_ = 0.8f;
	float elapsedTime_ = 0.0f;
	size_t visibleCount_ = 0;
	bool active_ = false;
};

class BossSummonCrystal final {
public:
	BossSummonCrystal();
	~BossSummonCrystal();
	void Initialize(const Vector3& position, float delay);
	void ScheduleBurst(float delayFromNow);
	bool Update(float deltaTime);
	Engine::Graphics3D::Object3D* GetRenderObject() const
	{
		return active_ ? object_.get() : nullptr;
	}
	const Vector3& GetPosition() const { return position_; }

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	Vector3 position_{};
	float elapsedTime_ = 0.0f;
	float delay_ = 0.0f;
	bool burstScheduled_ = false;
	bool active_ = false;
};

class BossSlamCube final {
public:
	BossSlamCube();
	~BossSlamCube();
	void Initialize(const Vector3& position, float delay);
	bool Update(float deltaTime);
	void Draw() const;
	Engine::Graphics3D::Object3D* GetRenderObject() const
	{
		return active_ && elapsedTime_ >= delay_ ? object_.get() : nullptr;
	}

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	Vector3 position_{};
	float elapsedTime_ = 0.0f;
	float delay_ = 0.0f;
	bool active_ = false;
};

} // namespace DirectXGame
