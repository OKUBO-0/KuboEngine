#include "CombatEffectsPresentation.h"
#include "Object3DCommon.h"
#include "ParticleManager.h"
#include "Line.h"
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

constexpr float kSwordSlashLifetime = 0.18f;
constexpr float kPi = 3.14159265359f;

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

void DrawGroundCircle(
	Engine::LineSystem::Line& line,
	const Vector3& center,
	float radius,
	float yOffset,
	const Vector4& color,
	int32_t segments)
{
	const float y = center.y + yOffset;
	for (int32_t segmentIndex = 0; segmentIndex < segments; ++segmentIndex) {
		const float startAngle =
			static_cast<float>(segmentIndex) /
			static_cast<float>(segments) * kPi * 2.0f;
		const float endAngle =
			static_cast<float>(segmentIndex + 1) /
			static_cast<float>(segments) * kPi * 2.0f;
		line.Draw(
			{
				center.x + std::cos(startAngle) * radius,
				y,
				center.z + std::sin(startAngle) * radius,
			},
			{
				center.x + std::cos(endAngle) * radius,
				y,
				center.z + std::sin(endAngle) * radius,
			},
			color);
	}
}

void DrawFlameStrokes(
	Engine::LineSystem::Line& line,
	const DirectXGame::FlameZoneVisual& visual,
	float radius,
	float lifeRatio)
{
	const Vector3 forward = visual.direction;
	const Vector3 side{
		-forward.z,
		0.0f,
		forward.x,
	};
	const Vector4 flameColor{
		1.0f,
		0.42f + lifeRatio * 0.24f,
		0.04f,
		0.72f,
	};
	for (int32_t index = -1; index <= 1; ++index) {
		const Vector3 base =
			visual.position +
			side * (static_cast<float>(index) * radius * 0.26f) -
			forward * (radius * 0.18f);
		const Vector3 tip =
			base +
			forward * (radius * 0.34f) +
			Vector3{ 0.0f, 0.18f + lifeRatio * 0.10f, 0.0f };
		line.Draw(
			base + side * (radius * 0.10f),
			tip,
			flameColor);
		line.Draw(
			base - side * (radius * 0.10f),
			tip,
			flameColor);
	}
}

void QueueFlameShoeZoneCircles(
	const DirectXGame::PlayerManager& playerManager)
{
	Engine::LineSystem::Line line;
	for (const DirectXGame::FlameZoneVisual& visual :
		playerManager.GetFlameZoneVisuals()) {
		if (visual.radius <= 0.0f || visual.remainingDuration <= 0.0f) {
			continue;
		}
		const float totalDuration = (std::max)(0.001f, visual.totalDuration);
		const float lifeRatio =
			Clamp01(visual.remainingDuration / totalDuration);
		const float pulse =
			0.96f + std::sin((1.0f - lifeRatio) * kPi * 4.0f) * 0.04f;
		const float radius = visual.radius * pulse;
		const Vector4 outerColor{
			1.0f,
			0.18f + lifeRatio * 0.30f,
			0.02f,
			0.72f,
		};
		const Vector4 innerColor{
			1.0f,
			0.56f + lifeRatio * 0.18f,
			0.06f,
			0.50f,
		};
		DrawGroundCircle(line, visual.position, radius, 0.08f, outerColor, 44);
		DrawGroundCircle(
			line,
			visual.position,
			radius * (0.50f + lifeRatio * 0.18f),
			0.10f,
			innerColor,
			32);
		DrawFlameStrokes(line, visual, radius, lifeRatio);
	}
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
	swordSlashVisuals_.reserve(kMaxSwordSlashVisuals);
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
	const std::vector<SwordSlashEvent>& swordSlashes =
		playerManager.GetRecentSwordSlashes();
	for (size_t slashIndex = 0; slashIndex < swordSlashes.size(); ++slashIndex) {
		const SwordSlashEvent& slash = swordSlashes[slashIndex];
		const Vector3 side{
			-slash.forward.z,
			0.0f,
			slash.forward.x,
		};
		const Vector3 slashCenter =
			slash.center + slash.forward * (slash.radius * 0.58f);
		particleManager->Emit(handles.enemyHitSpark, slashCenter, 8u);
		particleManager->Emit(
			handles.enemyHitSpark,
			slashCenter + side * (slash.radius * 0.26f),
			4u);
		particleManager->Emit(
			handles.enemyHitSpark,
			slashCenter - side * (slash.radius * 0.26f),
			4u);
		particleManager->Emit(handles.ripple, slashCenter, 1u);
		SpawnSwordSlashVisual(
			slash.center,
			slash.forward,
			slash.radius,
			slash.directionSign);
	}
	if (playerManager.DidAuraPulseThisFrame()) {
		particleManager->Emit(handles.ripple, playerPosition, 1u);
	}
	QueueFlameShoeZoneCircles(playerManager);
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
	UpdateSwordSlashVisuals(1.0f / 60.0f);
	UpdateLightningVisuals(playerManager);

	return bossPhaseChanged;
}

void CombatEffectsPresentation::Draw() const
{
	for (const SwordSlashVisual& slash : swordSlashVisuals_) {
		if (slash.age >= slash.lifetime) {
			continue;
		}
		for (const auto& segment : slash.segments) {
			if (segment && segment->GetColor().w > 0.0f) {
				segment->Draw();
			}
		}
	}
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& object :
		lightningObjects_) {
		if (object && object->GetColor().w > 0.0f) {
			object->Draw();
		}
	}
}

void CombatEffectsPresentation::SpawnSwordSlashVisual(
	const Vector3& center,
	const Vector3& forward,
	float radius,
	int32_t directionSign)
{
	if (swordSlashVisuals_.size() >= kMaxSwordSlashVisuals) {
		swordSlashVisuals_.erase(swordSlashVisuals_.begin());
	}

	SwordSlashVisual visual{};
	const ModelHandle slashHandle = GameModelCache::Load("cube.obj");
	for (auto& segment : visual.segments) {
		segment = std::make_unique<Engine::Graphics3D::Object3D>();
		segment->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*segment, slashHandle);
		segment->SetSkyboxFilePath("Resources/textures/skybox/test.dds");
		segment->SetEnvironmentReflectionStrength(0.0f);
		segment->SetEnvironmentRoughness(1.0f);
		segment->SetLighting(false);
		segment->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	visual.center = center;
	visual.forward = forward;
	visual.radius = radius;
	visual.lifetime = kSwordSlashLifetime;
	visual.directionSign = directionSign >= 0 ? 1 : -1;
	swordSlashVisuals_.push_back(std::move(visual));
}

void CombatEffectsPresentation::UpdateSwordSlashVisuals(float deltaTime)
{
	for (SwordSlashVisual& slash : swordSlashVisuals_) {
		slash.age += deltaTime;
		const float progress = Clamp01(slash.age / slash.lifetime);
		const float alpha = 1.0f - progress;
		const Vector3 side{
			-slash.forward.z,
			0.0f,
			slash.forward.x,
		};
		const float yaw = std::atan2(slash.forward.x, slash.forward.z);
		const float sweepStart =
			static_cast<float>(slash.directionSign) * 1.22f;
		const float sweepEnd =
			static_cast<float>(slash.directionSign) * -1.22f;
		const float sweepOffset =
			static_cast<float>(slash.directionSign) * (0.34f - progress * 0.18f);
		for (size_t segmentIndex = 0;
			segmentIndex < slash.segments.size();
			++segmentIndex) {
			Engine::Graphics3D::Object3D* segment =
				slash.segments[segmentIndex].get();
			if (!segment) {
				continue;
			}
			const float t = slash.segments.size() <= 1
				? 0.0f
				: static_cast<float>(segmentIndex) /
					static_cast<float>(slash.segments.size() - 1);
			const float arcAngle =
				sweepStart + (sweepEnd - sweepStart) * t + sweepOffset;
			const float taper = std::sin(t * 3.14159265359f);
			const float segmentAlpha = alpha * std::clamp(taper * 1.35f, 0.0f, 1.0f);
			const float arcRadius = slash.radius * (0.52f + progress * 0.08f);
			const Vector3 local =
				slash.forward * (std::cos(arcAngle) * arcRadius) +
				side * (std::sin(arcAngle) * arcRadius);
			const Vector3 tangent =
				slash.forward * (-std::sin(arcAngle)) +
				side * (std::cos(arcAngle));
			const float segmentYaw = std::atan2(tangent.x, tangent.z);
			const float segmentLength = slash.radius *
				(0.25f + taper * 0.18f);
			const float segmentThickness = slash.radius *
				(0.10f + taper * 0.06f);
			const Vector3 position =
				slash.center +
				slash.forward * (slash.radius * 0.42f) +
				local;

			segment->SetScale({
				segmentLength,
				segmentThickness,
				segmentThickness * 0.55f,
				});
			segment->SetRotate({ 0.0f, segmentYaw + 1.57079632679f, 0.0f });
			segment->SetTranslate({
				position.x,
				position.y + 1.05f,
				position.z,
				});
			segment->SetColor({
				1.0f,
				1.0f,
				1.0f,
				segmentAlpha * 0.9f,
				});
			segment->Update();
		}
	}
	std::erase_if(swordSlashVisuals_, [](const SwordSlashVisual& slash) {
		return slash.age >= slash.lifetime;
	});
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
