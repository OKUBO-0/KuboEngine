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
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 160> objects_;
	size_t visibleCount_ = 0;
	bool visible_ = false;
};

class BossAreaTelegraph final {
public:
	BossAreaTelegraph();
	~BossAreaTelegraph();
	void Update(const BossAttackTelegraph& telegraph);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized(size_t count);
	void EnsureCrystals();
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 768> objects_;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 4> crystalObjects_;
	size_t visibleCount_ = 0;
	size_t crystalVisibleCount_ = 0;
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
		float length);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized(size_t count);
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 160> objects_;
	Vector3 position_{};
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float length_ = 34.0f;
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
		const Vector4& color);
	bool Update(float deltaTime);
	void AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const;

private:
	void EnsureInitialized();
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 96> objects_;
	Vector3 center_{};
	Vector4 color_{ 1.0f, 0.35f, 0.04f, 0.9f };
	float radius_ = 8.0f;
	float delay_ = 0.0f;
	float elapsedTime_ = 0.0f;
	bool active_ = false;
};

class BossSummonCrystal final {
public:
	BossSummonCrystal();
	~BossSummonCrystal();
	void Initialize(const Vector3& position, float delay);
	bool Update(float deltaTime);
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
