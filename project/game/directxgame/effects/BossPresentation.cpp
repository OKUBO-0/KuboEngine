#include "BossPresentation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "ParticleManager.h"
#include "GameParticleEffects.h"
#include "EnemyManager.h"
#include "Player.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace {

constexpr float kEntranceDuration = 3.4f;
constexpr float kEntranceDropStart = 0.28f;
constexpr float kEntranceDropEnd = 0.68f;
constexpr float kEntranceHoldEnd = 0.82f;
constexpr float kEntranceCameraDistance = 42.0f;
constexpr float kEntranceCameraHeight = 22.0f;
constexpr float kEntranceFocusHeight = 7.0f;
constexpr float kEntranceBossDropHeight = 64.0f;
constexpr float kDefeatDuration = 3.0f;
constexpr float kDefeatTransitionStartTime = 3.35f;
constexpr float kDefeatCameraDistance = 23.0f;
constexpr float kDefeatCameraHeight = 10.5f;
constexpr float kDefeatFocusHeight = 3.0f;

float Clamp01(float value)
{
	return (std::clamp)(value, 0.0f, 1.0f);
}

Vector3 Lerp(const Vector3& start, const Vector3& end, float t)
{
	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t,
	};
}

float SmoothStep(float value)
{
	const float t = Clamp01(value);
	return t * t * (3.0f - 2.0f * t);
}

Vector3 LookAtRotation(const Vector3& cameraPosition, const Vector3& focusPosition)
{
	const Vector3 direction{
		focusPosition.x - cameraPosition.x,
		focusPosition.y - cameraPosition.y,
		focusPosition.z - cameraPosition.z,
	};
	const float horizontalLength = std::sqrt(
		direction.x * direction.x + direction.z * direction.z);
	return {
		std::atan2(-direction.y, horizontalLength),
		std::atan2(direction.x, direction.z),
		0.0f,
	};
}

float LengthXZ(const Vector3& value)
{
	return std::sqrt(value.x * value.x + value.z * value.z);
}

Vector3 NormalizeXZOrDefault(const Vector3& value, const Vector3& fallback)
{
	const float length = LengthXZ(value);
	if (length <= 0.001f) {
		return fallback;
	}
	return { value.x / length, 0.0f, value.z / length };
}

}

namespace DirectXGame {

void BossPresentation::Reset()
{
	entranceTimer_ = 0.0f;
	defeatTimer_ = 0.0f;
	entranceEffectEmitted_ = false;
	defeatEffectEmitted_ = false;
	resultTransitionRequested_ = false;
}

void BossPresentation::StartEntrance(
	EnemyManager& enemyManager,
	const Player* player)
{
	Reset();
	enemyManager.StartBossPhase();
	enemyManager.GetBossPresentationPosition(entranceFocusPosition_);
	entrancePlayerPosition_ = player
		? player->GetWorldPosition()
		: Vector3{
			entranceFocusPosition_.x,
			0.0f,
			entranceFocusPosition_.z - 26.0f,
		};
	entranceStartBossPosition_ = entranceFocusPosition_;
	entranceStartBossPosition_.y += kEntranceBossDropHeight;
	entranceCurrentBossPosition_ = entranceStartBossPosition_;
	enemyManager.SetBossPresentationPosition(entranceStartBossPosition_);
	const Vector3 focusPosition{
		entranceStartBossPosition_.x,
		entranceStartBossPosition_.y + kEntranceFocusHeight,
		entranceStartBossPosition_.z,
	};
	entranceStartCameraPosition_ = {
		entranceFocusPosition_.x,
		entranceFocusPosition_.y + kEntranceBossDropHeight + kEntranceCameraHeight + 8.0f,
		entranceFocusPosition_.z - kEntranceCameraDistance - 18.0f,
	};
	entranceStartCameraRotation_ =
		LookAtRotation(entranceStartCameraPosition_, focusPosition);
	entranceReturnCameraPosition_ = entranceStartCameraPosition_;
	entranceReturnCameraRotation_ = entranceStartCameraRotation_;
	if (player) {
		const PlayerCameraController::CameraPose returnPose =
			player->CalculateCameraPoseFacingTarget(entranceFocusPosition_);
		entranceReturnCameraPosition_ = returnPose.position;
		entranceReturnCameraRotation_ = returnPose.rotation;
	}
	UpdateEntranceCamera(0.0f);
}

bool BossPresentation::UpdateEntrance(
	EnemyManager& enemyManager,
	const GameParticleEffects& particleEffects,
	float deltaTime)
{
	entranceTimer_ += deltaTime;
	const float entranceProgress = Clamp01(entranceTimer_ / kEntranceDuration);
	const float bossDropProgress = SmoothStep(
		(entranceProgress - kEntranceDropStart) /
		(kEntranceDropEnd - kEntranceDropStart));
	entranceCurrentBossPosition_ = Lerp(
		entranceStartBossPosition_, entranceFocusPosition_, bossDropProgress);
	enemyManager.SetBossPresentationPosition(entranceCurrentBossPosition_);

	if (!entranceEffectEmitted_ &&
		entranceProgress >= kEntranceDropEnd) {
		const GameParticleEffects::Handles& handles =
			particleEffects.GetHandles();
		Engine::Particle::ParticleManager* particleManager =
			Engine::Particle::ParticleManager::GetInstance();
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("enemyDeath"),
			entranceFocusPosition_,
			42);
		particleManager->Emit(
			particleEffects.GetSmokeBindingHandle("bossEntranceSmoke"),
			entranceFocusPosition_,
			18);
		particleManager->Emit(handles.ripple, entranceFocusPosition_, 3);
		entranceEffectEmitted_ = true;
	}

	UpdateEntranceCamera(entranceProgress);
	if (entranceTimer_ >= kEntranceDuration) {
		enemyManager.ClearBossPresentationPositionOverride();
		return true;
	}
	return false;
}

void BossPresentation::StartDefeat(EnemyManager& enemyManager)
{
	defeatTimer_ = 0.0f;
	defeatEffectEmitted_ = false;
	resultTransitionRequested_ = false;
	enemyManager.ClearBossPresentationPositionOverride();
	enemyManager.GetBossPresentationPosition(defeatFocusPosition_);
	enemyManager.UpdateBossDeathPresentation(0.0f, kDefeatDuration);
	defeatStartCameraPosition_ = {
		defeatFocusPosition_.x - 10.0f,
		defeatFocusPosition_.y + kDefeatCameraHeight + 7.0f,
		defeatFocusPosition_.z - kDefeatCameraDistance - 12.0f,
	};
	defeatStartCameraRotation_ = LookAtRotation(
		defeatStartCameraPosition_,
		{
			defeatFocusPosition_.x,
			defeatFocusPosition_.y + kDefeatFocusHeight,
			defeatFocusPosition_.z,
		});
}

bool BossPresentation::UpdateDefeat(
	EnemyManager& enemyManager,
	const GameParticleEffects& particleEffects,
	float deltaTime)
{
	defeatTimer_ += deltaTime;
	Vector3 bossPosition{};
	if (enemyManager.GetBossPresentationPosition(bossPosition)) {
		defeatFocusPosition_ = bossPosition;
	}
	enemyManager.UpdateBossDeathPresentation(defeatTimer_, kDefeatDuration);

	if (!defeatEffectEmitted_) {
		const GameParticleEffects::Tuning& tuning = particleEffects.GetTuning();
		const GameParticleEffects::Handles& handles = particleEffects.GetHandles();
		Engine::Particle::ParticleManager* particleManager =
			Engine::Particle::ParticleManager::GetInstance();
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("enemyDeath"),
			defeatFocusPosition_,
			static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSparkCount * 2)));
		particleManager->Emit(
			particleEffects.GetSmokeBindingHandle("bossDeathSmoke"),
			defeatFocusPosition_,
			static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSmokeCount * 2)));
		particleManager->Emit(handles.ripple, defeatFocusPosition_, 2);
		defeatEffectEmitted_ = true;
	}

	UpdateDefeatCamera(Clamp01(defeatTimer_ / kDefeatDuration));
	if (!resultTransitionRequested_ && defeatTimer_ >= kDefeatTransitionStartTime) {
		resultTransitionRequested_ = true;
		return true;
	}
	return false;
}

void BossPresentation::CaptureCamera(Vector3& position, Vector3& rotation)
{
	if (Engine::CameraSystem::Camera* camera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) {
		position = camera->GetTransform().translate;
		rotation = camera->GetTransform().rotate;
	}
}

void BossPresentation::UpdateEntranceCamera(float progress)
{
	Engine::CameraSystem::Camera* camera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!camera) {
		return;
	}

	float returnProgress = 0.0f;
	if (progress > kEntranceHoldEnd) {
		returnProgress = Clamp01(
			(progress - kEntranceHoldEnd) / (1.0f - kEntranceHoldEnd));
	}
	const Vector3 toBoss = NormalizeXZOrDefault(
		{
			entranceFocusPosition_.x - entrancePlayerPosition_.x,
			0.0f,
			entranceFocusPosition_.z - entrancePlayerPosition_.z,
		},
		{ 0.0f, 0.0f, 1.0f });
	const Vector3 side{ toBoss.z, 0.0f, -toBoss.x };

	const Vector3 playerRevealPosition{
		entrancePlayerPosition_.x - toBoss.x * 7.0f + side.x * 3.2f,
		entrancePlayerPosition_.y + 5.4f,
		entrancePlayerPosition_.z - toBoss.z * 7.0f + side.z * 3.2f,
	};
	const Vector3 playerRevealFocus{
		entrancePlayerPosition_.x + toBoss.x * 10.0f,
		entrancePlayerPosition_.y + 4.4f,
		entrancePlayerPosition_.z + toBoss.z * 10.0f,
	};
	const Vector3 lookUpPosition{
		entranceFocusPosition_.x - toBoss.x * 25.0f + side.x * 6.5f,
		entranceFocusPosition_.y + 7.0f,
		entranceFocusPosition_.z - toBoss.z * 25.0f + side.z * 6.5f,
	};
	const Vector3 lookUpFocus{
		entranceStartBossPosition_.x,
		entranceStartBossPosition_.y + 8.0f,
		entranceStartBossPosition_.z,
	};
	const Vector3 fallTrackPosition{
		entranceFocusPosition_.x - toBoss.x * 30.0f + side.x * 8.0f,
		entranceFocusPosition_.y + 13.5f,
		entranceFocusPosition_.z - toBoss.z * 30.0f + side.z * 8.0f,
	};
	const Vector3 fallTrackFocus{
		entranceCurrentBossPosition_.x,
		entranceCurrentBossPosition_.y + kEntranceFocusHeight,
		entranceCurrentBossPosition_.z,
	};
	const Vector3 impactPosition{
		entranceFocusPosition_.x - toBoss.x * 19.0f + side.x * 5.2f,
		entranceFocusPosition_.y + 7.4f,
		entranceFocusPosition_.z - toBoss.z * 19.0f + side.z * 5.2f,
	};
	const Vector3 impactFocus{
		entranceCurrentBossPosition_.x,
		entranceCurrentBossPosition_.y + kEntranceFocusHeight,
		entranceCurrentBossPosition_.z,
	};
	const Vector3 holdPosition{
		entranceFocusPosition_.x - toBoss.x * 23.0f + side.x * 5.8f,
		entranceFocusPosition_.y + 8.8f,
		entranceFocusPosition_.z - toBoss.z * 23.0f + side.z * 5.8f,
	};
	const Vector3 holdFocus{
		entranceFocusPosition_.x,
		entranceFocusPosition_.y + kEntranceFocusHeight,
		entranceFocusPosition_.z,
	};

	Vector3 cinematicPosition = playerRevealPosition;
	Vector3 cinematicFocus = playerRevealFocus;
	if (progress < kEntranceDropStart) {
		const float t = SmoothStep(progress / kEntranceDropStart);
		cinematicPosition = Lerp(playerRevealPosition, lookUpPosition, t);
		cinematicFocus = Lerp(playerRevealFocus, lookUpFocus, t);
	} else if (progress < kEntranceDropEnd) {
		const float t = SmoothStep(
			(progress - kEntranceDropStart) /
			(kEntranceDropEnd - kEntranceDropStart));
		cinematicPosition = Lerp(lookUpPosition, fallTrackPosition, t);
		cinematicFocus = Lerp(lookUpFocus, fallTrackFocus, t);
	} else if (progress < kEntranceHoldEnd) {
		const float t = SmoothStep(
			(progress - kEntranceDropEnd) /
			(kEntranceHoldEnd - kEntranceDropEnd));
		cinematicPosition = Lerp(impactPosition, holdPosition, t);
		cinematicFocus = Lerp(impactFocus, holdFocus, t);
	} else {
		cinematicPosition = holdPosition;
		cinematicFocus = holdFocus;
	}
	const Vector3 cinematicRotation =
		LookAtRotation(cinematicPosition, cinematicFocus);
	const float returnBlend = SmoothStep(returnProgress);
	camera->SetTranslate(Lerp(
		cinematicPosition,
		entranceReturnCameraPosition_,
		returnBlend));
	camera->SetRotate(Lerp(
		cinematicRotation,
		entranceReturnCameraRotation_,
		returnBlend));
	camera->SetFarClip(500.0f);
	camera->Update();
}

void BossPresentation::UpdateDefeatCamera(float progress)
{
	Engine::CameraSystem::Camera* camera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!camera) {
		return;
	}

	const float eased = progress * progress * (3.0f - 2.0f * progress);
	const Vector3 targetPosition{
		defeatFocusPosition_.x,
		defeatFocusPosition_.y + kDefeatCameraHeight,
		defeatFocusPosition_.z - kDefeatCameraDistance,
	};
	camera->SetTranslate(Lerp(defeatStartCameraPosition_, targetPosition, eased));
	camera->SetRotate(Lerp(
		defeatStartCameraRotation_,
		LookAtRotation(
			targetPosition,
			{
				defeatFocusPosition_.x,
				defeatFocusPosition_.y + kDefeatFocusHeight,
				defeatFocusPosition_.z,
			}),
		eased));
	camera->SetFarClip(500.0f);
	camera->Update();
}

}
