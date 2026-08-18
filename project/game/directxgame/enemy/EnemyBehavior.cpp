#include "EnemyBehavior.h"
#include "Enemy.h"
#include "GameAudioCache.h"
#include "Player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

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

void PlayBossAttackSound(BossAttackType type)
{
	switch (type) {
	case BossAttackType::Beam:
	case BossAttackType::TripleBeam: {
		static SoundHandle handle =
			GameAudioCache::LoadWave("se/boss_rush.wav");
		GameAudioCache::PlayTuned(handle, "boss.rush", 0.58f, 0.12f);
		break;
	}
	case BossAttackType::LeapShockwave:
	case BossAttackType::SummonLeapShockwave:
	case BossAttackType::DomeBurst: {
		static SoundHandle handle =
			GameAudioCache::LoadWave("se/boss_slam.wav");
		GameAudioCache::PlayTuned(handle, "boss.slam", 0.72f, 0.12f);
		break;
	}
	case BossAttackType::BulletHell:
	case BossAttackType::ConvergingShockwave: {
		static SoundHandle handle =
			GameAudioCache::LoadWave("se/boss_ink.wav");
		GameAudioCache::PlayTuned(handle, "boss.ink", 0.64f, 0.12f);
		break;
	}
	default:
		break;
	}
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
			enemy.ClearBossAttackTelegraph();
			return;
		}

		const int32_t phase = enemy.GetBossPhase();
		if (phase != currentPhase_) {
			currentPhase_ = phase;
			state_ = State::Stalk;
			stateTimer_ = GetStalkDuration(phase);
			currentAction_ = Action::Beam;
			enemy.ClearBossAttackTelegraph();
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
			if (!IsLeapAction(currentAction_) && currentAction_ != Action::Beam) {
				lockedTargetPosition_ = playerPosition;
			}
			attackDirection_ = NormalizeXZ({
				lockedTargetPosition_.x - position.x,
				0.0f,
				lockedTargetPosition_.z - position.z,
				});
			if (attackDirection_.x == 0.0f && attackDirection_.z == 0.0f) {
				attackDirection_ = toPlayer;
			}
			enemy.SetRotationY(std::atan2(attackDirection_.x, attackDirection_.z));
			const float pulse = 0.5f + 0.5f * std::sin(phaseTime_ * 22.0f);
			const BossAttackType telegraphType = ToAttackType(currentAction_, phase);
			const float telegraphDistance = currentAction_ == Action::Beam
				? std::sqrt(
					(lockedTargetPosition_.x - position.x) *
						(lockedTargetPosition_.x - position.x) +
					(lockedTargetPosition_.z - position.z) *
						(lockedTargetPosition_.z - position.z))
				: distance;
			enemy.SetBossAttackTelegraph(
				telegraphType,
				attackDirection_,
				1.0f - stateTimer_ / telegraphDuration_,
				GetTelegraphRange(currentAction_, phase, telegraphDistance),
				lockedTargetPosition_);
			enemy.SetBehaviorVisual(GetActionColor(currentAction_), 1.08f + pulse * 0.1f);
			if (stateTimer_ <= 0.0f) {
				ExecuteTelegraphedAction(enemy, phase);
			}
			return;
		}

		if (state_ == State::BeamReposition) {
			enemy.ClearBossAttackTelegraph();
			const float desiredDistance = GetBeamPreferredDistance(phase);
			const float distanceError = distance - desiredDistance;
			const Vector3 away{ -toPlayer.x, 0.0f, -toPlayer.z };
			const float strafeSign =
				std::sin(phaseTime_ * 5.1f) >= 0.0f ? 1.0f : -1.0f;
			const Vector3 side{ -toPlayer.z * strafeSign, 0.0f, toPlayer.x * strafeSign };
			const float distanceWeight = std::clamp(std::abs(distanceError) / 12.0f, 0.4f, 2.3f);
			const float directionSign = distanceError < 0.0f ? 1.0f : -1.0f;
			Vector3 movement = NormalizeXZ({
				away.x * directionSign * distanceWeight + side.x * 0.42f,
				0.0f,
				away.z * directionSign * distanceWeight + side.z * 0.42f,
				});
			enemy.SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
			enemy.SetBehaviorVisual({ 1.0f, 1.0f, 1.0f, 1.0f }, 1.04f);
			MoveEnemy(enemy, movement, ToFrameScaledSpeed(enemy.GetSpeed() * 2.35f, deltaTime));
			if (stateTimer_ <= 0.0f ||
				std::abs(distanceError) <= GetBeamDistanceTolerance(phase)) {
				state_ = State::Telegraph;
				telegraphDuration_ = GetTelegraphDuration(currentAction_, phase);
				stateTimer_ = telegraphDuration_;
				const Vector3 newPosition = enemy.GetPosition();
				lockedTargetPosition_ = playerPosition;
				attackDirection_ = NormalizeXZ({
					lockedTargetPosition_.x - newPosition.x,
					0.0f,
					lockedTargetPosition_.z - newPosition.z,
					});
				if (attackDirection_.x == 0.0f && attackDirection_.z == 0.0f) {
					attackDirection_ = toPlayer;
				}
			}
			return;
		}

		if (state_ == State::Leap) {
			leapTimer_ += deltaTime;
			const float progress = std::clamp(leapTimer_ / leapDuration_, 0.0f, 1.0f);
			const float arc = std::sin(progress * std::numbers::pi_v<float>) * GetLeapHeight(currentPhase_);
			Vector3 leapPosition{
				leapStartPosition_.x + (lockedTargetPosition_.x - leapStartPosition_.x) * progress,
				arc,
				leapStartPosition_.z + (lockedTargetPosition_.z - leapStartPosition_.z) * progress,
			};
			enemy.SetPosition(leapPosition);
			enemy.SetRotationY(std::atan2(attackDirection_.x, attackDirection_.z));
			enemy.SetBehaviorVisual(GetActionColor(currentAction_), 1.18f);
			if (stateTimer_ <= 0.0f) {
				enemy.SetPosition({ lockedTargetPosition_.x, 0.0f, lockedTargetPosition_.z });
				enemy.NotifyLand();
				PlayBossAttackSound(BossAttackType::LeapShockwave);
				enemy.QueueBossAttack(ToAttackType(currentAction_, phase), attackDirection_, lockedTargetPosition_);
				BeginRecovery(phase);
			}
			return;
		}

		if (state_ == State::Recovery) {
			enemy.ClearBossAttackTelegraph();
			enemy.SetBehaviorVisual(
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				phase == 3 ? 1.08f : 1.0f);
			if (stateTimer_ <= 0.0f) {
				state_ = State::Stalk;
				stateTimer_ = GetStalkDuration(phase);
			}
			return;
		}

		if (stateTimer_ <= 0.0f) {
			currentAction_ = SelectNextAction(phase);
			attackDirection_ = toPlayer;
			lockedTargetPosition_ = playerPosition;
			if (currentAction_ == Action::Beam &&
				ShouldRepositionForBeam(distance, phase)) {
				state_ = State::BeamReposition;
				stateTimer_ = GetBeamRepositionDuration(phase);
			} else {
				state_ = State::Telegraph;
				telegraphDuration_ = GetTelegraphDuration(currentAction_, phase);
				stateTimer_ = telegraphDuration_;
			}
			return;
		}

		const float strafeSign = std::sin(phaseTime_ * (phase >= 4 ? 3.2f : 1.7f)) >= 0.0f ? 1.0f : -1.0f;
		enemy.ClearBossAttackTelegraph();
		const Vector3 side{ -toPlayer.z * strafeSign, 0.0f, toPlayer.x * strafeSign };
		float approach = distance > 14.0f ? 2.2f
			: (distance > 9.0f ? 0.8f : (distance < 6.0f ? -0.55f : 0.25f));
		if (phase >= 4) {
			approach += 0.35f;
		}
		Vector3 movement = NormalizeXZ({
			toPlayer.x * approach + side.x,
			0.0f,
			toPlayer.z * approach + side.z,
		});
		const float moveMultiplier = phase >= 5 ? 2.05f : (phase >= 4 ? 1.8f : (phase >= 2 ? 1.45f : 1.1f));
		enemy.SetBehaviorVisual(
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			phase >= 4 ? 1.12f : (phase >= 2 ? 1.07f : 1.0f));
		MoveEnemy(enemy, movement, ToFrameScaledSpeed(enemy.GetSpeed() * moveMultiplier, deltaTime));
	}

private:
	enum class Action {
		Beam,
		LeapShockwave,
		BulletHell,
		DomeBurst,
		ConvergingShockwave,
	};

	enum class State {
		Stalk,
		BeamReposition,
		Telegraph,
		Leap,
		Recovery,
	};

	Action SelectNextAction(int32_t phase)
	{
		const std::array<Action, 5> actions{
			Action::Beam,
			Action::LeapShockwave,
			Action::BulletHell,
			Action::DomeBurst,
			Action::ConvergingShockwave,
		};
		const int32_t actionCount =
			phase <= 1 ? 2 :
			(phase == 2 ? 3 :
			(phase == 3 ? 4 : 5));
		for (int32_t attempt = 0; attempt < actionCount; ++attempt) {
			const Action candidate = actions[static_cast<size_t>(selectionIndex_ % actionCount)];
			++selectionIndex_;
			if (candidate != previousAction_) {
				previousAction_ = candidate;
				return candidate;
			}
		}
		return Action::Beam;
	}

	static float GetTelegraphDuration(Action action, int32_t phase)
	{
		switch (action) {
		case Action::Beam: return phase >= 3 ? 0.96f : 0.86f;
		case Action::LeapShockwave: return 0.88f;
		case Action::BulletHell: return 0.82f;
		case Action::ConvergingShockwave: return 1.05f;
		case Action::DomeBurst: return 1.9f;
		default: return 0.85f;
		}
	}

	static float GetTelegraphRange(Action action, int32_t phase, float distance)
	{
		switch (action) {
		case Action::Beam:
			return (std::max)(
				phase >= 3 ? 70.0f : 62.0f,
				distance + 28.0f);
		case Action::LeapShockwave:
			return phase >= 4 ? 155.0f : 135.0f;
		case Action::BulletHell: return 22.0f;
		case Action::ConvergingShockwave: return 58.0f;
		case Action::DomeBurst: return 58.0f;
		default: return 12.0f;
		}
	}

	static BossAttackType ToAttackType(Action action, int32_t phase)
	{
		switch (action) {
		case Action::Beam:
			return phase >= 3 ? BossAttackType::TripleBeam : BossAttackType::Beam;
		case Action::LeapShockwave:
			return phase >= 4 ? BossAttackType::SummonLeapShockwave : BossAttackType::LeapShockwave;
		case Action::BulletHell: return BossAttackType::BulletHell;
		case Action::ConvergingShockwave: return BossAttackType::ConvergingShockwave;
		case Action::DomeBurst: return BossAttackType::DomeBurst;
		default: return BossAttackType::Beam;
		}
	}

	static Vector4 GetActionColor(Action action)
	{
		(void)action;
		return { 1.0f, 0.18f, 0.08f, 1.0f };
	}

	static bool IsLeapAction(Action action)
	{
		return action == Action::LeapShockwave;
	}

	static float GetLeapHeight(int32_t phase)
	{
		return phase >= 4 ? 12.0f : 8.5f;
	}

	static float GetBeamPreferredDistance(int32_t phase)
	{
		return phase >= 3 ? 34.0f : 30.0f;
	}

	static float GetBeamDistanceTolerance(int32_t phase)
	{
		return phase >= 3 ? 6.0f : 5.0f;
	}

	static bool ShouldRepositionForBeam(float distance, int32_t phase)
	{
		const float preferred = GetBeamPreferredDistance(phase);
		const float tolerance = GetBeamDistanceTolerance(phase);
		return distance < preferred - tolerance ||
			distance > preferred + tolerance;
	}

	static float GetBeamRepositionDuration(int32_t phase)
	{
		return phase >= 4 ? 0.82f : 0.70f;
	}

	void ExecuteTelegraphedAction(Enemy& enemy, int32_t phase)
	{
		const BossAttackType attackType = ToAttackType(currentAction_, phase);
		if (IsLeapAction(currentAction_)) {
			enemy.ClearBossAttackTelegraph();
			state_ = State::Leap;
			leapTimer_ = 0.0f;
			leapDuration_ = phase >= 4 ? 0.62f : 0.54f;
			stateTimer_ = leapDuration_;
			leapStartPosition_ = enemy.GetPosition();
			enemy.NotifyJump();
			return;
		}
		enemy.ClearBossAttackTelegraph();
		PlayBossAttackSound(attackType);
		enemy.QueueBossAttack(attackType, attackDirection_, lockedTargetPosition_);
		BeginRecovery(phase);
	}

	void BeginRecovery(int32_t phase)
	{
		state_ = State::Recovery;
		if (currentAction_ == Action::BulletHell) {
			stateTimer_ = 4.05f;
			return;
		}
		stateTimer_ = phase >= 5 ? 0.34f : (phase >= 3 ? 0.48f : 0.62f);
	}

	static float GetStalkDuration(int32_t phase)
	{
		return phase >= 5 ? 0.42f : (phase >= 4 ? 0.55f : (phase >= 2 ? 0.8f : 1.05f));
	}

	State state_ = State::Stalk;
	Action currentAction_ = Action::Beam;
	Action previousAction_ = Action::ConvergingShockwave;
	int32_t currentPhase_ = 1;
	int32_t selectionIndex_ = 0;
	float phaseTime_ = 0.0f;
	float stateTimer_ = 1.2f;
	float telegraphDuration_ = 0.55f;
	float leapTimer_ = 0.0f;
	float leapDuration_ = 0.55f;
	Vector3 attackDirection_{ 0.0f, 0.0f, 1.0f };
	Vector3 lockedTargetPosition_{};
	Vector3 leapStartPosition_{};
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
