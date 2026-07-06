#include "EnemyBehavior.h"
#include "Enemy.h"
#include "Player.h"
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

		const int32_t phase = enemy.GetBossPhase();
		if (phase != currentPhase_) {
			currentPhase_ = phase;
			state_ = State::Stalk;
			stateTimer_ = GetStalkDuration(phase);
			currentAction_ = Action::BasicRush;
			rushesRemaining_ = 0;
		}

		phaseTime_ += deltaTime;
		stateTimer_ -= deltaTime;
		const Vector3 position = enemy.GetPosition();
		const Vector3 playerPosition = player->GetWorldPosition();
		Vector3 toPlayer{ playerPosition.x - position.x, 0.0f, playerPosition.z - position.z };
		const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
		toPlayer = NormalizeXZ(toPlayer);
		if (toPlayer.x == 0.0f && toPlayer.z == 0.0f) {
			toPlayer = { 0.0f, 0.0f, 1.0f };
		}

		if (state_ == State::Telegraph) {
			rushDirection_ = toPlayer;
			enemy.SetRotationY(std::atan2(rushDirection_.x, rushDirection_.z));
			const float pulse = 0.5f + 0.5f * std::sin(phaseTime_ * 22.0f);
			const BossAttackType telegraphType = currentAction_ == Action::TentacleSlam
				? BossAttackType::TentacleSlam
				: (currentAction_ == Action::InkBurst
					? BossAttackType::InkBurst
					: BossAttackType::Rush);
			const float telegraphRange = telegraphType == BossAttackType::TentacleSlam
				? 30.0f
				: (telegraphType == BossAttackType::Rush ? distance + 12.0f : 8.0f);
			enemy.SetBossAttackTelegraph(
				telegraphType,
				rushDirection_,
				1.0f - stateTimer_ / telegraphDuration_,
				telegraphRange);
			const Vector4 telegraphColor = currentAction_ == Action::InkBurst
				? Vector4{ 0.28f, 0.12f, 0.42f, 1.0f }
				: (currentAction_ == Action::TentacleSlam
					? Vector4{ 1.0f, 0.34f, 0.12f, 1.0f }
					: Vector4{ 1.0f, 0.78f + pulse * 0.18f, 0.16f, 1.0f });
			enemy.SetBehaviorVisual(telegraphColor, 2.35f + pulse * 0.28f);
			if (stateTimer_ <= 0.0f) {
				ExecuteTelegraphedAction(enemy, phase, distance);
			}
			return;
		}

		if (state_ == State::Rush) {
			const float rushMultiplier =
				(phase == 3 ? 4.2f : (phase == 2 ? 3.5f : 2.9f)) *
				rushDistanceSpeedScale_;
			enemy.SetBehaviorVisual(
				phase == 3 ? Vector4{ 1.0f, 0.12f, 0.12f, 1.0f } : Vector4{ 1.0f, 0.42f, 0.18f, 1.0f },
				phase == 3 ? 2.8f : 2.55f);
			MoveEnemy(enemy, rushDirection_, ToFrameScaledSpeed(enemy.GetSpeed() * rushMultiplier, deltaTime));
			if (stateTimer_ <= 0.0f) {
				--rushesRemaining_;
				if (rushesRemaining_ > 0) {
					state_ = State::Telegraph;
					telegraphDuration_ = 0.28f;
					stateTimer_ = telegraphDuration_;
				} else {
					BeginRecovery(phase);
				}
			}
			return;
		}

		if (state_ == State::Recovery) {
			enemy.SetBehaviorVisual(
				{ 0.38f, 0.38f, 0.46f, 1.0f },
				phase == 3 ? 2.35f : 2.15f);
			if (stateTimer_ <= 0.0f) {
				state_ = State::Stalk;
				stateTimer_ = GetStalkDuration(phase);
			}
			return;
		}

		if (stateTimer_ <= 0.0f) {
			currentAction_ = SelectNextAction(phase);
			state_ = State::Telegraph;
			telegraphDuration_ = GetTelegraphDuration(currentAction_, phase);
			stateTimer_ = telegraphDuration_;
			rushDirection_ = toPlayer;
			return;
		}

		const float strafeSign = std::sin(phaseTime_ * (phase == 3 ? 2.8f : 1.7f)) >= 0.0f ? 1.0f : -1.0f;
		const Vector3 side{ -toPlayer.z * strafeSign, 0.0f, toPlayer.x * strafeSign };
		float approach = distance > 14.0f ? 2.2f
			: (distance > 9.0f ? 0.8f : (distance < 6.0f ? -0.55f : 0.25f));
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
	enum class Action {
		BasicRush,
		TentacleSlam,
		InkBurst,
		TripleRush,
	};

	enum class State {
		Stalk,
		Telegraph,
		Rush,
		Recovery,
	};

	Action SelectNextAction(int32_t phase)
	{
		const int32_t actionCount = phase == 1 ? 2 : (phase == 2 ? 3 : 4);
		for (int32_t attempt = 0; attempt < actionCount; ++attempt) {
			const Action candidate = static_cast<Action>(selectionIndex_ % actionCount);
			++selectionIndex_;
			if (candidate != previousAction_) {
				previousAction_ = candidate;
				return candidate;
			}
		}
		return Action::BasicRush;
	}

	static float GetTelegraphDuration(Action action, int32_t phase)
	{
		switch (action) {
		case Action::TentacleSlam: return 0.75f;
		case Action::InkBurst: return 0.85f;
		case Action::TripleRush: return 0.42f;
		default: return phase == 3 ? 0.35f : (phase == 2 ? 0.45f : 0.55f);
		}
	}

	void ExecuteTelegraphedAction(
		Enemy& enemy,
		int32_t phase,
		float distance)
	{
		const float normalizedDistance = std::clamp(distance / 16.0f, 0.85f, 1.6f);
		rushDistanceSpeedScale_ = normalizedDistance;
		const float rushMultiplier =
			phase == 3 ? 4.2f : (phase == 2 ? 3.5f : 2.9f);
		const float rushUnitsPerSecond =
			enemy.GetSpeed() * rushMultiplier * rushDistanceSpeedScale_ / 0.016f;
		const float rushDuration = std::clamp(
			(distance + 12.0f) / (std::max)(1.0f, rushUnitsPerSecond),
			0.34f,
			2.2f);
		switch (currentAction_) {
		case Action::TentacleSlam:
			enemy.QueueBossAttack(BossAttackType::TentacleSlam, rushDirection_);
			BeginRecovery(phase);
			break;
		case Action::InkBurst:
			enemy.QueueBossAttack(BossAttackType::InkBurst, rushDirection_);
			BeginRecovery(phase);
			break;
		case Action::TripleRush:
			if (rushesRemaining_ <= 0) {
				rushesRemaining_ = 3;
			}
			state_ = State::Rush;
			stateTimer_ = rushDuration;
			break;
		default:
			rushesRemaining_ = 1;
			state_ = State::Rush;
			stateTimer_ = rushDuration;
			break;
		}
	}

	void BeginRecovery(int32_t phase)
	{
		state_ = State::Recovery;
		if (currentAction_ == Action::TripleRush) {
			stateTimer_ = 0.72f;
		} else if (currentAction_ == Action::TentacleSlam ||
			currentAction_ == Action::InkBurst) {
			stateTimer_ = 0.55f;
		} else {
			stateTimer_ = phase == 3 ? 0.3f : (phase == 2 ? 0.36f : 0.42f);
		}
	}

	static float GetStalkDuration(int32_t phase)
	{
		return phase == 3 ? 0.55f : (phase == 2 ? 0.8f : 1.05f);
	}

	State state_ = State::Stalk;
	Action currentAction_ = Action::BasicRush;
	Action previousAction_ = Action::TripleRush;
	int32_t currentPhase_ = 1;
	int32_t selectionIndex_ = 0;
	int32_t rushesRemaining_ = 0;
	float phaseTime_ = 0.0f;
	float stateTimer_ = 1.2f;
	float telegraphDuration_ = 0.55f;
	float rushDistanceSpeedScale_ = 1.0f;
	Vector3 rushDirection_{ 0.0f, 0.0f, 1.0f };
};

} // namespace

std::unique_ptr<IEnemyBehavior> CreateEnemyBehavior(EnemyBehaviorType type)
{
	if (type == EnemyBehaviorType::Tackle) {
		return std::make_unique<BurstChaseEnemyBehavior>();
	}
	if (type == EnemyBehaviorType::Boss) {
		return std::make_unique<BossEnemyBehavior>();
	}
	return std::make_unique<ChaseEnemyBehavior>();
}

} // namespace DirectXGame
