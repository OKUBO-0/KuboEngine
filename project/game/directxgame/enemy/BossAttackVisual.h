#pragma once

#include "Enemy.h"
#include "Vector3.h"
#include <memory>

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

private:
	void EnsureInitialized();
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	bool visible_ = false;
};

class BossSlamCube final {
public:
	BossSlamCube();
	~BossSlamCube();
	void Initialize(const Vector3& position, float delay);
	bool Update(float deltaTime);
	void Draw() const;

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	Vector3 position_{};
	float elapsedTime_ = 0.0f;
	float delay_ = 0.0f;
	bool active_ = false;
};

} // namespace DirectXGame
