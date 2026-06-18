#pragma once

#include "Vector2.h"
#include "Vector3.h"

namespace DirectXGame {

class PlayerMovementController final {
public:
	void Update(
		float deltaTime,
		Vector3& position,
		float& rotationY,
		float moveSpeedPerSecond);
	bool IsDodging() const { return dodgeTimer_ > 0.0f; }
	float GetDodgeCooldownRatio() const;
	void SuppressNextDodgeTrigger()
	{
		suppressNextDodgeTrigger_ = true;
	}

private:
	void UpdateDodge(
		float deltaTime,
		const Vector2& moveInput,
		float& rotationY);

	Vector3 dodgeDirection_{ 0.0f, 0.0f, 1.0f };
	float dodgeTimer_ = 0.0f;
	float dodgeCooldownTimer_ = 0.0f;
	bool suppressNextDodgeTrigger_ = false;

	static constexpr float kDodgeDuration = 0.22f;
	static constexpr float kDodgeCooldown = 0.8f;
	static constexpr float kDodgeSpeed = 78.0f;
};

}
