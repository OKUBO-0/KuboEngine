#include "BossPresentation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "ParticleManager.h"
#include "GameParticleEffects.h"
#include "EnemyManager.h"
#include <algorithm>
#include <cstdint>

namespace {

constexpr float kEntranceDuration = 1.6f;
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

void BossPresentation::StartEntrance(EnemyManager& enemyManager)
{
	Reset();
	enemyManager.StartBossPhase();
	enemyManager.GetBossPresentationPosition(entranceFocusPosition_);
	CaptureCamera(entranceStartCameraPosition_, entranceStartCameraRotation_);
}

bool BossPresentation::UpdateEntrance(
	EnemyManager& enemyManager,
	const GameParticleEffects& particleEffects,
	float deltaTime)
{
	entranceTimer_ += deltaTime;
	enemyManager.GetBossPresentationPosition(entranceFocusPosition_);

	if (!entranceEffectEmitted_) {
		const GameParticleEffects::Handles& handles =
			particleEffects.GetHandles();
		Engine::Particle::ParticleManager* particleManager =
			Engine::Particle::ParticleManager::GetInstance();
		particleManager->Emit(handles.enemyHitSpark, entranceFocusPosition_, 42);
		particleManager->Emit(handles.deathSmoke, entranceFocusPosition_, 18);
		particleManager->Emit(handles.ripple, entranceFocusPosition_, 3);
		entranceEffectEmitted_ = true;
	}

	UpdateEntranceCamera(Clamp01(entranceTimer_ / kEntranceDuration));
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
			handles.enemyHitSpark,
			defeatFocusPosition_,
			static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSparkCount * 2)));
		particleManager->Emit(
			handles.deathSmoke,
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

	const float eased = progress * progress * (3.0f - 2.0f * progress);
	const float blend =
		progress < 0.72f ? eased : Clamp01((1.0f - progress) / 0.28f);
	const Vector3 targetPosition{
		entranceFocusPosition_.x,
		entranceFocusPosition_.y + 18.0f,
		entranceFocusPosition_.z - 24.0f,
	};
	camera->SetTranslate(Lerp(entranceStartCameraPosition_, targetPosition, blend));
	camera->SetRotate(Lerp(
		entranceStartCameraRotation_, Vector3{ 0.62f, 0.0f, 0.0f }, blend));
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
