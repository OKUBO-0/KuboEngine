#pragma once

#include "Vector3.h"
#include <cstdint>
#include <memory>

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class EnemyDeathBomb final {
public:
	void Initialize(
		const Vector3& position,
		float delay,
		float radius,
		int32_t damage);
	bool Update(float deltaTime);
	void Draw() const;
	Engine::Graphics3D::Object3D* GetRenderObject() const
	{
		return !exploded_ ? object_.get() : nullptr;
	}

	const Vector3& GetPosition() const { return position_; }
	float GetRadius() const { return radius_; }
	int32_t GetDamage() const { return damage_; }

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	Vector3 position_{};
	float delay_ = 1.25f;
	float radius_ = 8.0f;
	float elapsedTime_ = 0.0f;
	int32_t damage_ = 18;
	bool exploded_ = false;
};

} // namespace DirectXGame
