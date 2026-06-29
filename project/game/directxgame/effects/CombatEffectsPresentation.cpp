#include "CombatEffectsPresentation.h"
#include "Object3DCommon.h"
#include "ParticleManager.h"
#include "GameModelCache.h"
#include "GameParticleEffects.h"
#include "Enemy.h"
#include "EnemyManager.h"
#include "Player.h"
#include "PlayerManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void CombatEffectsPresentation::Initialize()
{
	const ModelHandle modelHandle = GameModelCache::Load("cube.obj");
	for (std::unique_ptr<Engine::Graphics3D::Object3D>& object : lightningObjects_) {
		object = std::make_unique<Engine::Graphics3D::Object3D>();
		object->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*object, modelHandle);
		object->SetSkyboxFilePath("Resources/textures/skybox/test.dds");
		object->SetEnvironmentReflectionStrength(0.0f);
		object->SetEnvironmentRoughness(1.0f);
		object->SetLighting(false);
		object->SetColor({ 0.42f, 0.84f, 1.0f, 0.0f });
	}
}

void CombatEffectsPresentation::Reset(const PlayerManager& playerManager)
{
	previousHp_ = playerManager.GetHP();
	previousTotalExp_ = playerManager.GetTotalEXP();
	previousLightningTimer_ = playerManager.GetLightningEffectTimer();
}

bool CombatEffectsPresentation::Update(
	Player& player,
	PlayerManager& playerManager,
	EnemyManager* enemyManager,
	const GameParticleEffects& particleEffects)
{
	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	const GameParticleEffects::Tuning& tuning = particleEffects.GetTuning();
	const GameParticleEffects::Handles& handles = particleEffects.GetHandles();
	bool bossPhaseChanged = false;

	if (enemyManager) {
		const std::vector<Vector3>& hitPositions =
			enemyManager->GetRecentHitEffectPositions();
		for (const Vector3& hitPosition : hitPositions) {
			particleManager->Emit(
				handles.enemyHitSpark,
				hitPosition,
				static_cast<uint32_t>((std::max)(0, tuning.enemyHitSparkCount)));
		}
		if (!hitPositions.empty()) {
			const float hitStrength = (std::min)(
				0.78f,
				0.34f + static_cast<float>(hitPositions.size()) * 0.08f);
			player.RequestCameraShake(0.105f, hitStrength);
		}
		for (const Vector3& deathPosition :
			enemyManager->GetRecentDeathEffectPositions()) {
			particleManager->Emit(
				handles.enemyHitSpark,
				deathPosition,
				static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSparkCount)));
			particleManager->Emit(
				handles.deathSmoke,
				deathPosition,
				static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSmokeCount)));
		}
		for (const Vector3& explosionPosition :
			enemyManager->GetRecentExplosionEffectPositions()) {
			particleManager->Emit(handles.enemyHitSpark, explosionPosition, 36u);
			particleManager->Emit(handles.deathSmoke, explosionPosition, 14u);
			particleManager->Emit(handles.ripple, explosionPosition, 3u);
		}
		enemyManager->ClearRecentEffectPositions();

		Vector3 phasePosition{};
		int32_t phase = 0;
		if (enemyManager->ConsumeBossPhaseChanged(phasePosition, phase)) {
			particleManager->Emit(
				handles.enemyHitSpark,
				phasePosition,
				phase == 3 ? 64u : 42u);
			particleManager->Emit(
				handles.deathSmoke,
				phasePosition,
				phase == 3 ? 20u : 12u);
			particleManager->Emit(
				handles.ripple,
				phasePosition,
				phase == 3 ? 4u : 2u);
			bossPhaseChanged = true;
		}
		for (const std::unique_ptr<Enemy>& enemy : enemyManager->GetEnemies()) {
			if (!enemy || !enemy->IsActive() || !enemy->IsSuicideType()) {
				continue;
			}
			particleManager->EmitTrailSegment(
				handles.suicideEnemyTrail,
				enemy->GetPreviousPosition(),
				enemy->GetPosition(),
				0.34f);
		}
	}

	const Vector3 playerPosition = player.GetWorldPosition();
	const int32_t hp = playerManager.GetHP();
	if (hp < previousHp_) {
		particleManager->Emit(
			handles.spark,
			playerPosition,
			static_cast<uint32_t>((std::max)(0, tuning.playerDamageSparkCount)));
		particleManager->Emit(
			handles.ripple,
			playerPosition,
			static_cast<uint32_t>((std::max)(0, tuning.playerDamageRippleCount)));
	}
	previousHp_ = hp;

	const int32_t totalExp = playerManager.GetTotalEXP();
	if (totalExp > previousTotalExp_) {
		particleManager->Emit(
			handles.expSpark,
			playerPosition,
			static_cast<uint32_t>((std::max)(0, tuning.expSparkCount)));
	}
	previousTotalExp_ = totalExp;

	const float lightningTimer = playerManager.GetLightningEffectTimer();
	if (lightningTimer > previousLightningTimer_) {
		for (const Vector3& target : playerManager.GetLightningEffectTargets()) {
			particleManager->Emit(
				handles.lightningImpact,
				target,
				static_cast<uint32_t>((std::max)(0, tuning.lightningSparkCount)));
		}
	}
	previousLightningTimer_ = lightningTimer;
	UpdateLightningVisuals(playerManager);

	return bossPhaseChanged;
}

void CombatEffectsPresentation::Draw() const
{
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& object :
		lightningObjects_) {
		if (object && object->GetColor().w > 0.0f) {
			object->Draw();
		}
	}
}

void CombatEffectsPresentation::UpdateLightningVisuals(
	const PlayerManager& playerManager)
{
	const float timer = playerManager.GetLightningEffectTimer();
	const std::vector<Vector3>& targets =
		playerManager.GetLightningEffectTargets();
	const float alpha = timer > 0.0f ? Clamp01(timer / 0.22f) : 0.0f;

	for (size_t targetIndex = 0;
		targetIndex < kLightningMaxTargets;
		++targetIndex) {
		const bool active =
			targetIndex < targets.size() &&
			alpha > 0.0f;
		const Vector3 target = active ? targets[targetIndex] : Vector3{};
		for (size_t segmentIndex = 0;
			segmentIndex < kLightningSegmentCount;
			++segmentIndex) {
			const size_t objectIndex =
				targetIndex * kLightningSegmentCount + segmentIndex;
			Engine::Graphics3D::Object3D* object =
				lightningObjects_[objectIndex].get();
			if (!object) {
				continue;
			}
			if (!active) {
				object->SetColor({ 0.42f, 0.84f, 1.0f, 0.0f });
				object->Update();
				continue;
			}

			constexpr float kSegmentHeight = 3.1f;
			const float side = segmentIndex % 2 == 0 ? -0.34f : 0.34f;
			const float pulse =
				0.82f +
				std::sin(
					timer * 110.0f +
					static_cast<float>(segmentIndex)) *
					0.18f;
			object->SetScale({
				0.22f * pulse,
				kSegmentHeight * 0.58f,
				0.22f * pulse,
			});
			object->SetRotate({ 0.0f, 0.0f, side * 0.14f });
			object->SetTranslate({
				target.x + side,
				target.y +
					1.0f +
					kSegmentHeight *
						(static_cast<float>(segmentIndex) + 0.5f),
				target.z,
			});
			object->SetColor({
				0.48f + pulse * 0.18f,
				0.82f,
				1.0f,
				alpha,
			});
			object->Update();
		}
	}
}

}
