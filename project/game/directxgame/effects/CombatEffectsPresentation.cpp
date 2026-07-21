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

bool EmitClampedTrail(
	Engine::Particle::ParticleManager* particleManager,
	Engine::Particle::ParticleGroupHandle handle,
	const Vector3& previous,
	const Vector3& current,
	float width,
	float lengthMultiplier,
	float maxFrameDistance)
{
	if (!particleManager) {
		return false;
	}
	return particleManager->EmitTrailSegmentClamped(
		handle,
		previous,
		current,
		width,
		lengthMultiplier,
		maxFrameDistance);
}

void EmitCircleTrail(
	Engine::Particle::ParticleManager* particleManager,
	Engine::Particle::ParticleGroupHandle handle,
	const Vector3& center,
	float radius,
	float yOffset,
	float width,
	int32_t segments)
{
	if (!particleManager || radius <= 0.0f || segments <= 0) {
		return;
	}
	particleManager->EmitTrailCircle(
		handle,
		center,
		radius,
		yOffset,
		width,
		segments);
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

void DrawThickGroundCircle(
	Engine::LineSystem::Line& line,
	const Vector3& center,
	float radius,
	float yOffset,
	const Vector4& color,
	int32_t segments,
	float thickness,
	int32_t layers)
{
	if (radius <= 0.0f || layers <= 0) {
		return;
	}
	const float layerCount = static_cast<float>((std::max)(1, layers));
	for (int32_t layerIndex = 0; layerIndex < layers; ++layerIndex) {
		const float centered =
			static_cast<float>(layerIndex) -
			(layerCount - 1.0f) * 0.5f;
		const float layerRadius =
			(std::max)(0.05f, radius + centered * thickness);
		Vector4 layerColor = color;
		layerColor.w *= 1.0f - std::abs(centered) / layerCount * 0.32f;
		DrawGroundCircle(
			line,
			center,
			layerRadius,
			yOffset + static_cast<float>(layerIndex) * 0.006f,
			layerColor,
			segments);
	}
}

void EmitCirclePoints(
	Engine::Particle::ParticleManager* particleManager,
	Engine::Particle::ParticleGroupHandle handle,
	const Vector3& center,
	float radius,
	float yOffset,
	uint32_t count,
	float phase)
{
	if (!particleManager || radius <= 0.0f || count == 0u) {
		return;
	}
	for (uint32_t index = 0; index < count; ++index) {
		const float angle =
			(static_cast<float>(index) / static_cast<float>(count) + phase) *
			kPi * 2.0f;
		particleManager->Emit(
			handle,
			{
				center.x + std::cos(angle) * radius,
				center.y + yOffset,
				center.z + std::sin(angle) * radius,
			},
			1u);
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
		line.Draw(
			base,
			tip + forward * (radius * 0.10f),
			{ 1.0f, 0.74f, 0.10f, flameColor.w * 0.72f });
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
			0.88f,
		};
		const Vector4 midColor{
			1.0f,
			0.34f + lifeRatio * 0.22f,
			0.03f,
			0.72f,
		};
		const Vector4 innerColor{
			1.0f,
			0.56f + lifeRatio * 0.18f,
			0.06f,
			0.62f,
		};
		DrawThickGroundCircle(
			line,
			visual.position,
			radius,
			0.08f,
			outerColor,
			40,
			radius * 0.018f,
			3);
		DrawThickGroundCircle(
			line,
			visual.position,
			radius * 0.78f,
			0.10f,
			midColor,
			32,
			radius * 0.015f,
			2);
		DrawThickGroundCircle(
			line,
			visual.position,
			radius * (0.50f + lifeRatio * 0.18f),
			0.10f,
			innerColor,
			32,
			radius * 0.012f,
			2);
		DrawFlameStrokes(line, visual, radius, lifeRatio);
	}
}

void QueueAuraCircle(
	const DirectXGame::Player& player,
	const DirectXGame::PlayerManager& playerManager)
{
	if (!playerManager.HasAura()) {
		return;
	}
	Engine::LineSystem::Line line;
	const float radius = playerManager.GetAuraRadius();
	const Vector3 center = player.GetWorldPosition();
	const Vector4 outerColor{ 0.34f, 0.82f, 1.0f, 0.84f };
	const Vector4 middleColor{ 0.52f, 0.95f, 1.0f, 0.56f };
	const Vector4 innerColor{ 0.82f, 1.0f, 1.0f, 0.40f };
	DrawThickGroundCircle(
		line,
		center,
		radius,
		0.07f,
			outerColor,
		56,
		radius * 0.010f,
		4);
	DrawThickGroundCircle(
		line,
		center,
		radius * 0.84f,
		0.085f,
			middleColor,
		44,
		radius * 0.008f,
		2);
	DrawThickGroundCircle(
		line,
		center,
		radius * 0.62f,
		0.10f,
		innerColor,
		48,
		radius * 0.006f,
		2);
}

void QueueExplosionRangeCircle(
	const Vector3& center,
	float radius)
{
	Engine::LineSystem::Line line;
	const Vector4 outerColor{ 1.0f, 0.30f, 0.04f, 0.90f };
	const Vector4 middleColor{ 1.0f, 0.62f, 0.08f, 0.66f };
	const Vector4 innerColor{ 1.0f, 0.92f, 0.34f, 0.42f };
	DrawGroundCircle(line, center, radius, 0.12f, outerColor, 64);
	DrawGroundCircle(line, center, radius * 0.68f, 0.14f, middleColor, 48);
	DrawGroundCircle(line, center, radius * 0.34f, 0.16f, innerColor, 32);
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
				particleEffects.GetSparkBindingHandle("enemyHit"),
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
				particleEffects.GetSparkBindingHandle("enemyDeath"),
				deathPosition,
				static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSparkCount)));
			particleManager->Emit(
				particleEffects.GetSmokeBindingHandle("enemyDeathSmoke"),
				deathPosition,
				static_cast<uint32_t>((std::max)(0, tuning.enemyDeathSmokeCount)));
		}
		for (const Vector3& explosionPosition :
			enemyManager->GetRecentExplosionEffectPositions()) {
			QueueExplosionRangeCircle(
				explosionPosition,
				playerManager.GetExplosiveBulletRadius());
			particleManager->Emit(
				handles.explosionBurst,
				explosionPosition,
				static_cast<uint32_t>((std::max)(0, tuning.explosionBurstCount)));
			particleManager->Emit(
				particleEffects.GetSmokeBindingHandle("explosionSmoke"),
				explosionPosition,
				static_cast<uint32_t>((std::max)(0, tuning.explosionSmokeCount)));
			particleManager->Emit(handles.ripple, explosionPosition, 4u);
			player.RequestCameraShake(0.13f, 0.62f);
		}
		enemyManager->ClearRecentEffectPositions();

		Vector3 phasePosition{};
		int32_t phase = 0;
		if (enemyManager->ConsumeBossPhaseChanged(phasePosition, phase)) {
			particleManager->Emit(
				particleEffects.GetSparkBindingHandle("enemyDeath"),
				phasePosition,
				phase == 3 ? 64u : 42u);
			particleManager->Emit(
				particleEffects.GetSmokeBindingHandle("bossDeathSmoke"),
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
	QueueAuraCircle(player, playerManager);
	const GameParticleEffects::Tuning& bowTrailTuning = particleEffects.GetTuning();
	for (const std::unique_ptr<NormalBullet>& bullet :
		playerManager.GetNormalBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("bowProjectile"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			bowTrailTuning.bowTrailWidth,
			bowTrailTuning.bowTrailLengthMultiplier,
			bowTrailTuning.bowTrailMaxFrameDistance);
	}
	for (const std::unique_ptr<NormalBullet>& bullet :
		playerManager.GetExplosiveBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("flameProjectile"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			0.30f,
			1.15f,
			6.0f);
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("flameProjectileGlow"),
			bullet->GetPosition(),
			2u);
	}
	for (const std::unique_ptr<NormalBullet>& bullet :
		playerManager.GetHandgunBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("handgunProjectile"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			0.13f,
			1.20f,
			8.0f);
	}
	for (const Vector3& shotPosition :
		playerManager.GetRecentHandgunShotPositions()) {
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("handgunMuzzleFlash"),
			shotPosition,
			7u);
		particleManager->Emit(
			handles.handgunBulletTrail,
			shotPosition,
			3u);
	}
	for (const Vector3& reloadPosition :
		playerManager.GetRecentHandgunReloadPositions()) {
		particleManager->Emit(
			particleEffects.GetSmokeBindingHandle("handgunReloadSmoke"),
			reloadPosition,
			5u);
	}
	for (const std::unique_ptr<NormalBullet>& bullet :
		playerManager.GetBoomerangBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("boomerangProjectile"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			0.22f,
			1.28f,
			8.0f);
	}
	for (const std::unique_ptr<NormalBullet>& bullet :
		playerManager.GetBoneBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("boneProjectile"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			0.20f,
			1.45f,
			7.0f);
	}
	for (const std::unique_ptr<OrbitBullet>& bullet :
		playerManager.GetOrbitBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		EmitClampedTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("rockOrbit"),
			bullet->GetPreviousPosition(),
			bullet->GetPosition(),
			0.18f,
			1.05f,
			5.0f);
	}
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
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("enemyHit"),
			slashCenter,
			8u);
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("enemyHit"),
			slashCenter + side * (slash.radius * 0.26f),
			4u);
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("enemyHit"),
			slashCenter - side * (slash.radius * 0.26f),
			4u);
		particleManager->Emit(handles.ripple, slashCenter, 1u);
		particleManager->EmitTrailSegment(
			particleEffects.GetTrailBindingHandle("swordSlash"),
			slash.center + side * (slash.radius * 0.46f),
			slashCenter - side * (slash.radius * 0.46f),
			0.18f);
		particleManager->EmitTrailSegment(
			particleEffects.GetTrailBindingHandle("swordSlash"),
			slash.center + slash.forward * (slash.radius * 0.24f),
			slashCenter + slash.forward * (slash.radius * 0.42f),
			0.12f);
		SpawnSwordSlashVisual(
			slash.center,
			slash.forward,
			slash.radius,
			slash.directionSign);
	}
	if (playerManager.DidAuraPulseThisFrame()) {
		particleManager->Emit(handles.ripple, playerPosition, 1u);
		EmitCircleTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("auraPulse"),
			playerPosition,
			playerManager.GetAuraRadius(),
			0.64f,
			0.26f,
			18);
		EmitCirclePoints(
			particleManager,
			particleEffects.GetSparkBindingHandle("auraGlow"),
			playerPosition,
			playerManager.GetAuraRadius() * 0.86f,
			0.34f,
			6u,
			0.04f);
	} else if (playerManager.HasAura()) {
		EmitCirclePoints(
			particleManager,
			particleEffects.GetSparkBindingHandle("auraGlow"),
			playerPosition,
			playerManager.GetAuraRadius() * 0.95f,
			0.32f,
			1u,
			0.0f);
	}
	QueueFlameShoeZoneCircles(playerManager);
	for (const DirectXGame::FlameZoneVisual& visual :
		playerManager.GetFlameZoneVisuals()) {
		if (visual.radius <= 0.0f || visual.remainingDuration <= 0.0f) {
			continue;
		}
		const float lifeRatio = Clamp01(
			visual.remainingDuration /
			(std::max)(0.001f, visual.totalDuration));
		EmitCirclePoints(
			particleManager,
			particleEffects.GetSparkBindingHandle("flameShoeGlow"),
			visual.position,
			visual.radius * (0.42f + lifeRatio * 0.24f),
			0.16f,
			lifeRatio > 0.45f ? 2u : 1u,
			1.0f - lifeRatio);
	}
	for (const Vector3& flamePosition :
		playerManager.GetRecentFlameZoneSpawns()) {
		EmitCircleTrail(
			particleManager,
			particleEffects.GetTrailBindingHandle("flameShoeSpawn"),
			flamePosition,
			2.7f,
			0.28f,
			0.36f,
			16);
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("flameShoeGlow"),
			flamePosition,
			5u);
	}
	const int32_t hp = playerManager.GetHP();
	if (hp < previousHp_) {
		particleManager->Emit(
			particleEffects.GetSparkBindingHandle("playerDamage"),
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
			particleEffects.GetSparkBindingHandle("expPickup"),
			playerPosition,
			static_cast<uint32_t>((std::max)(0, tuning.expSparkCount)));
	}
	previousTotalExp_ = totalExp;

	const float lightningTimer = playerManager.GetLightningEffectTimer();
	if (lightningTimer > previousLightningTimer_) {
		for (const Vector3& target : playerManager.GetLightningEffectTargets()) {
			particleManager->EmitTrailSegment(
				particleEffects.GetTrailBindingHandle("lightningStrike"),
				{ target.x, target.y + 8.2f, target.z },
				{ target.x, target.y + 0.45f, target.z },
				0.24f);
			particleManager->EmitTrailSegment(
				particleEffects.GetTrailBindingHandle("lightningStrike"),
				{ target.x - 0.36f, target.y + 5.4f, target.z + 0.22f },
				{ target.x + 0.18f, target.y + 1.3f, target.z - 0.12f },
				0.12f);
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
