#pragma once

#include "Vector3.h"
#include <memory>

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class BossInkProjectile final {
public:
	BossInkProjectile();
	~BossInkProjectile();
	void Initialize(const Vector3& position, const Vector3& direction);
	void Update(float deltaTime);
	void Draw() const;
	Engine::Graphics3D::Object3D* GetRenderObject() const
	{
		return active_ ? object_.get() : nullptr;
	}
	const Vector3& GetPosition() const { return position_; }
	float GetCollisionRadius() const { return 1.1f; }
	bool IsActive() const { return active_; }
	void Deactivate() { active_ = false; }

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	Vector3 position_{};
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float lifetime_ = 0.0f;
	bool active_ = false;
};

} // namespace DirectXGame
