#include "game/directxgame/enemy/EnemyBehavior.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/player/Player.h"
#include <algorithm>
#include <cmath>

namespace DirectXGame {
namespace {

float ToFrameScaledSpeed(float unitsPerFrameBase, float deltaTime)
{
	return unitsPerFrameBase * (deltaTime / 0.016f);
}

Vector3 NormalizeXZ(Vector3 direction)
{
	const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
	if (length <= 0.001f) {
		return { 0.0f, 0.0f, 0.0f };
	}
	direction.x /= length;
	direction.y = 0.0f;
	direction.z /= length;
	return direction;
}

void MoveEnemy(Enemy& enemy, const Vector3& direction, float speed)
{
	Vector3 position = enemy.GetPosition();
	position.x += direction.x * speed;
	position.z += direction.z * speed;
	enemy.SetPosition(position);
	enemy.SetRotationY(std::atan2(direction.x, direction.z));
}

class ChaseEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		const Vector3 direction = NormalizeXZ({ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z });
		if (direction.x == 0.0f && direction.z == 0.0f) {
			return;
		}

		enemy.ClearBehaviorVisual();
		MoveEnemy(enemy, direction, ToFrameScaledSpeed(enemy.GetSpeed(), deltaTime));
	}
};

class BurstChaseEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		Vector3 toPlayer{ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z };
		const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
		toPlayer = NormalizeXZ(toPlayer);
		if (toPlayer.x == 0.0f && toPlayer.z == 0.0f) {
			return;
		}

		if (dashTimer_ > 0.0f) {
			dashTimer_ -= deltaTime;
			enemy.SetBehaviorVisual({ 1.0f, 0.55f, 0.3f, 1.0f }, 1.12f);
			MoveEnemy(enemy, dashDirection_, ToFrameScaledSpeed(enemy.GetSpeed() * 2.8f, deltaTime));
			return;
		}

		if (distance < 18.0f) {
			windupTimer_ += deltaTime;
			enemy.SetBehaviorVisual({ 1.0f, 0.9f, 0.35f, 1.0f }, 1.18f);
			if (windupTimer_ >= 0.55f) {
				dashDirection_ = toPlayer;
				dashTimer_ = 0.28f;
				windupTimer_ = 0.0f;
				return;
			}
		} else {
			windupTimer_ = (std::max)(0.0f, windupTimer_ - deltaTime * 1.5f);
			enemy.ClearBehaviorVisual();
		}

		MoveEnemy(enemy, toPlayer, ToFrameScaledSpeed(enemy.GetSpeed() * 0.75f, deltaTime));
	}

private:
	float windupTimer_ = 0.0f;
	float dashTimer_ = 0.0f;
	Vector3 dashDirection_{ 0.0f, 0.0f, 1.0f };
};

class CircleApproachEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		Vector3 toPlayer = NormalizeXZ({ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z });
		if (toPlayer.x == 0.0f && toPlayer.z == 0.0f) {
			return;
		}

		const float dx = playerPosition.x - position.x;
		const float dz = playerPosition.z - position.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		const Vector3 side{ -toPlayer.z, 0.0f, toPlayer.x };
		const float approachWeight = distance > 12.0f ? 1.2f : 0.55f;
		Vector3 finalDirection{
			toPlayer.x * approachWeight + side.x * 1.1f,
			0.0f,
			toPlayer.z * approachWeight + side.z * 1.1f
		};
		finalDirection = NormalizeXZ(finalDirection);
		if (finalDirection.x == 0.0f && finalDirection.z == 0.0f) {
			return;
		}

		enemy.SetBehaviorVisual({ 0.65f, 0.95f, 1.0f, 1.0f }, 0.96f);
		MoveEnemy(enemy, finalDirection, ToFrameScaledSpeed(enemy.GetSpeed() * 1.05f, deltaTime));
	}
};

class KeepDistanceRushEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		Vector3 toPlayer{ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z };
		const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
		toPlayer = NormalizeXZ(toPlayer);
		if (toPlayer.x == 0.0f && toPlayer.z == 0.0f) {
			return;
		}

		strafeSwapTimer_ -= deltaTime;
		if (strafeSwapTimer_ <= 0.0f) {
			strafeSign_ *= -1.0f;
			strafeSwapTimer_ = 1.4f;
		}

		rushTimer_ -= deltaTime;
		rushCooldown_ -= deltaTime;
		if (rushCooldown_ <= 0.0f && distance > 8.0f && distance < 22.0f) {
			rushTimer_ = 0.35f;
			rushCooldown_ = 2.3f;
		}

		const Vector3 side{ -toPlayer.z * strafeSign_, 0.0f, toPlayer.x * strafeSign_ };
		float towardWeight = 0.18f;
		if (distance > 16.0f) {
			towardWeight = 1.0f;
		} else if (distance < 9.0f) {
			towardWeight = -1.25f;
		}

		Vector3 moveDirection{
			toPlayer.x * towardWeight + side.x,
			0.0f,
			toPlayer.z * towardWeight + side.z
		};
		if (rushTimer_ > 0.0f) {
			moveDirection = toPlayer;
		}
		moveDirection = NormalizeXZ(moveDirection);
		if (moveDirection.x == 0.0f && moveDirection.z == 0.0f) {
			return;
		}

		const float speedMultiplier = rushTimer_ > 0.0f ? 2.1f : 1.15f;
		enemy.SetBehaviorVisual(
			rushTimer_ > 0.0f ? Vector4{ 1.0f, 0.35f, 0.35f, 1.0f } : Vector4{ 0.82f, 0.72f, 1.0f, 1.0f },
			rushTimer_ > 0.0f ? 1.2f : 1.05f);
		MoveEnemy(enemy, moveDirection, ToFrameScaledSpeed(enemy.GetSpeed() * speedMultiplier, deltaTime));
	}

private:
	float strafeSign_ = 1.0f;
	float strafeSwapTimer_ = 1.2f;
	float rushTimer_ = 0.0f;
	float rushCooldown_ = 1.6f;
};

class FloatingDiveEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		const Vector3 playerPosition = player->GetWorldPosition();
		if (!initialized_) {
			startPosition_ = enemy.GetPosition();
			startPosition_.y = kGroundY;
			targetPosition_ = { playerPosition.x, kGroundY, playerPosition.z };
			const float dx = targetPosition_.x - startPosition_.x;
			const float dz = targetPosition_.z - startPosition_.z;
			const float distance = std::sqrt(dx * dx + dz * dz);
			flightDuration_ = std::clamp(distance / (enemy.GetSpeed() * 105.0f), 1.35f, 2.25f);
			arcHeight_ = std::clamp(distance * 0.22f, 7.0f, 12.0f);
			initialized_ = true;
		}

		elapsedTime_ += deltaTime;
		const float t = std::clamp(elapsedTime_ / flightDuration_, 0.0f, 1.0f);
		if (!targetLocked_) {
			targetPosition_.x = playerPosition.x;
			targetPosition_.z = playerPosition.z;
			if (t >= kTargetLockProgress) {
				targetLocked_ = true;
			}
		}

		const float oneMinusT = 1.0f - t;
		Vector3 nextPosition{
			startPosition_.x * oneMinusT + targetPosition_.x * t,
			kGroundY + 4.0f * arcHeight_ * t * oneMinusT,
			startPosition_.z * oneMinusT + targetPosition_.z * t,
		};
		const Vector3 previousPosition = enemy.GetPosition();
		const Vector3 movement = nextPosition - previousPosition;
		enemy.SetPosition(nextPosition);
		enemy.SetRotationY(std::atan2(movement.x, movement.z));
		enemy.SetBehaviorVisual(
			targetLocked_ ? Vector4{ 1.0f, 0.28f, 0.2f, 1.0f } : Vector4{ 1.0f, 0.62f, 0.18f, 1.0f },
			0.62f + t * 0.12f);

		if (t >= 1.0f && nextPosition.y <= kGroundY + 0.001f) {
			enemy.DeactivateOnGroundImpact();
		}
	}

private:
	static constexpr float kGroundY = 0.0f;
	static constexpr float kTargetLockProgress = 0.70f;

	bool initialized_ = false;
	bool targetLocked_ = false;
	float elapsedTime_ = 0.0f;
	float flightDuration_ = 1.6f;
	float arcHeight_ = 9.0f;
	Vector3 startPosition_{};
	Vector3 targetPosition_{};
};

class BossEnemyBehavior final : public IEnemyBehavior {
public:
	void Update(Enemy& enemy, float deltaTime) override
	{
		Player* player = enemy.GetPlayer();
		if (!player) {
			return;
		}

		phaseTime_ += deltaTime;
		actionTimer_ -= deltaTime;
		const int32_t phase = enemy.GetBossPhase();
		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		Vector3 toPlayer{ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z };
		const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
		toPlayer = NormalizeXZ(toPlayer);
		if (toPlayer.x == 0.0f && toPlayer.z == 0.0f) {
			toPlayer = { 0.0f, 0.0f, 1.0f };
		}

		if (rushTimer_ > 0.0f) {
			rushTimer_ -= deltaTime;
			const float rushMultiplier = phase == 3 ? 4.2f : (phase == 2 ? 3.5f : 2.9f);
			enemy.SetBehaviorVisual(
				phase == 3 ? Vector4{ 1.0f, 0.12f, 0.12f, 1.0f } : Vector4{ 1.0f, 0.42f, 0.18f, 1.0f },
				phase == 3 ? 2.8f : 2.55f);
			MoveEnemy(enemy, rushDirection_, ToFrameScaledSpeed(enemy.GetSpeed() * rushMultiplier, deltaTime));
			return;
		}

		if (actionTimer_ <= 0.0f) {
			rushDirection_ = toPlayer;
			rushTimer_ = phase == 3 ? 0.42f : 0.32f;
			actionTimer_ = phase == 3 ? 0.75f : (phase == 2 ? 1.15f : 1.7f);
			return;
		}

		const float strafeSign = std::sin(phaseTime_ * (phase == 3 ? 2.8f : 1.7f)) >= 0.0f ? 1.0f : -1.0f;
		const Vector3 side{ -toPlayer.z * strafeSign, 0.0f, toPlayer.x * strafeSign };
		float approach = distance > 18.0f ? 1.1f : (distance < 10.0f ? -0.85f : 0.15f);
		if (phase == 3) {
			approach += 0.35f;
		}
		Vector3 movement = NormalizeXZ({
			toPlayer.x * approach + side.x,
			0.0f,
			toPlayer.z * approach + side.z,
			});
		const float moveMultiplier = phase == 3 ? 1.8f : (phase == 2 ? 1.45f : 1.1f);
		const Vector4 color = phase == 3
			? Vector4{ 1.0f, 0.18f, 0.22f, 1.0f }
			: (phase == 2 ? Vector4{ 1.0f, 0.55f, 0.16f, 1.0f } : Vector4{ 0.58f, 0.3f, 1.0f, 1.0f });
		enemy.SetBehaviorVisual(color, phase == 3 ? 2.6f : (phase == 2 ? 2.4f : 2.2f));
		MoveEnemy(enemy, movement, ToFrameScaledSpeed(enemy.GetSpeed() * moveMultiplier, deltaTime));
	}

private:
	float phaseTime_ = 0.0f;
	float actionTimer_ = 1.2f;
	float rushTimer_ = 0.0f;
	Vector3 rushDirection_{ 0.0f, 0.0f, 1.0f };
};

} // namespace

std::unique_ptr<IEnemyBehavior> CreateEnemyBehaviorByType(int32_t type)
{
	if (type == 1) {
		return std::make_unique<BurstChaseEnemyBehavior>();
	}
	if (type == 2) {
		return std::make_unique<CircleApproachEnemyBehavior>();
	}
	if (type == 3) {
		return std::make_unique<KeepDistanceRushEnemyBehavior>();
	}
	if (type == 4) {
		return std::make_unique<FloatingDiveEnemyBehavior>();
	}
	if (type == 5) {
		return std::make_unique<BossEnemyBehavior>();
	}
	return std::make_unique<ChaseEnemyBehavior>();
}

} // namespace DirectXGame
