#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class ExpOrb {
public:
	void Initialize(const Vector3& position, int32_t expValue);
	void Update(const Vector3& playerPosition, float deltaTime);
	void Draw();

	bool IsActive() const { return active_; }
	int32_t GetEXP() const { return expValue_; }
	const Vector3& GetPosition() const { return position_; }
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	void ApplyTransform();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 velocity_{ 0.0f, 0.0f, 0.0f };
	int32_t expValue_ = 0;
	bool active_ = true;
	float spin_ = 0.0f;

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	GameLightSettings lightSettings_{};
};

} // namespace DirectXGame
