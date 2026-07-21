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
constexpr float kEntranceApproachEnd = 0.32f;
constexpr float kEntranceHoldEnd = 0.72f;
constexpr float kEntranceCameraDistance = 32.0f;
constexpr float kEntranceCameraHeight = 20.0f;
constexpr float kEntranceFocusHeight = 5.0f;
constexpr float kEntranceBossDropHeight = 14.0f;
constexpr float kDefeatDuration = 1.45f;
constexpr float kDefeatTransitionStartTime = 1.55f;
constexpr float kDefeatCameraDistance = 30.0f;
constexpr float kDefeatCameraHeight = 22.0f;
constexpr float kDefeatCameraPitch = 0.68f;

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
	entranceStartBossPosition_ = entranceFocusPosition_;
	entranceStartBossPosition_.y += kEntranceBossDropHeight;
	enemyManager.SetBossPresentationPosition(entranceStartBossPosition_);
	CaptureCamera(entranceStartCameraPosition_, entranceStartCameraRotation_);
	entranceReturnCameraPosition_ = entranceStartCameraPosition_;
	entranceReturnCameraRotation_ = entranceStartCameraRotation_;
	if (player) {
		const PlayerCameraController::CameraPose returnPose =
			player->CalculateCameraPoseFacingTarget(entranceFocusPosition_);
		entranceReturnCameraPosition_ = returnPose.position;
		entranceReturnCameraRotation_ = returnPose.rotation;
	}
}

bool BossPresentation::UpdateEntrance(
	EnemyManager& enemyManager,
	const GameParticleEffects& particleEffects,
	float deltaTime)
{
	entranceTimer_ += deltaTime;
	const float entranceProgress = Clamp01(entranceTimer_ / kEntranceDuration);
	const float bossDropProgress = SmoothStep(entranceProgress / kEntranceApproachEnd);
	enemyManager.SetBossPresentationPosition(Lerp(
		entranceStartBossPosition_, entranceFocusPosition_, bossDropProgress));

	if (!entranceEffectEmitted_) {
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
	return entranceTimer_ >= kEntranceDuration;
}

void BossPresentation::StartDefeat(EnemyManager& enemyManager)
{
	defeatTimer_ = 0.0f;
	defeatEffectEmitted_ = false;
	resultTransitionRequested_ = false;
	enemyManager.GetBossPresentationPosition(defeatFocusPosition_);
	enemyManager.UpdateBossDeathPresentation(0.0f, kDefeatDuration);
	CaptureCamera(defeatStartCameraPosition_, defeatStartCameraRotation_);
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

	float approachBlend = 1.0f;
	float returnProgress = 0.0f;
	if (progress < kEntranceApproachEnd) {
		approachBlend = SmoothStep(progress / kEntranceApproachEnd);
	} else if (progress > kEntranceHoldEnd) {
		returnProgress = Clamp01(
			(progress - kEntranceHoldEnd) / (1.0f - kEntranceHoldEnd));
	}
	const float returnBlend = SmoothStep(returnProgress);
	const Vector3 focusPosition{
		entranceFocusPosition_.x,
		entranceFocusPosition_.y + kEntranceFocusHeight,
		entranceFocusPosition_.z,
	};
	const Vector3 targetPosition{
		entranceFocusPosition_.x,
		entranceFocusPosition_.y + kEntranceCameraHeight,
		entranceFocusPosition_.z - kEntranceCameraDistance,
	};
	const Vector3 targetRotation = LookAtRotation(targetPosition, focusPosition);
	const Vector3 holdPosition = Lerp(
		entranceStartCameraPosition_,
		targetPosition,
		approachBlend);
	const Vector3 holdRotation = Lerp(
		entranceStartCameraRotation_,
		targetRotation,
		approachBlend);
	camera->SetTranslate(Lerp(
		holdPosition,
		entranceReturnCameraPosition_,
		returnBlend));
	camera->SetRotate(Lerp(
		holdRotation,
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
		Vector3{ kDefeatCameraPitch, 0.0f, 0.0f },
		eased));
	camera->SetFarClip(500.0f);
	camera->Update();
}

}
