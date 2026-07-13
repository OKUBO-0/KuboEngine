#include "GameParticleEffects.h"
#include "ParticleBehaviors.h"
#include "ParticleManager.h"
#include <algorithm>
#include <memory>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void GameParticleEffects::Initialize()
{
	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	handles_.ripple = particleManager->CreateParticleGroup(
		"DirectXGame.Ripple", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Ring,
		std::make_unique<RippleParticleBehavior>(), 192);
	handles_.spark = particleManager->CreateParticleGroup(
		"DirectXGame.Spark", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(), 384);
	handles_.enemyHitSpark = particleManager->CreateParticleGroup(
		"DirectXGame.EnemyHitSpark", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.62f, 0.18f, 1.0f }), 384);
	handles_.expSpark = particleManager->CreateParticleGroup(
		"DirectXGame.ExpSpark", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 0.35f, 1.0f, 0.58f, 1.0f }), 256);
	handles_.lightningImpact = particleManager->CreateParticleGroup(
		"DirectXGame.LightningImpact", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<LightningImpactParticleBehavior>(), 256);
	handles_.explosionBurst = particleManager->CreateParticleGroup(
		"DirectXGame.ExplosionBurst", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<ExplosionBurstParticleBehavior>(), 512);
	SmokeParticleBehavior::Settings explosionSmokeSettings{};
	explosionSmokeSettings.lifetime = 0.92f;
	explosionSmokeSettings.offsetRange = 1.4f;
	explosionSmokeSettings.scaleMin = 0.68f;
	explosionSmokeSettings.scaleMax = 1.25f;
	explosionSmokeSettings.scaleGrow = 0.030f;
	explosionSmokeSettings.yOffset = 0.54f;
	handles_.explosionSmoke = particleManager->CreateParticleGroup(
		"DirectXGame.ExplosionSmoke", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SmokeParticleBehavior>(
			Vector4{ 0.62f, 0.42f, 0.30f, 0.72f },
			explosionSmokeSettings),
		256);
	handles_.playerDeathSpark = particleManager->CreateParticleGroup(
		"DirectXGame.PlayerDeathSpark", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.18f, 0.12f, 1.0f }), 256);
	handles_.deathSmoke = particleManager->CreateParticleGroup(
		"DirectXGame.DeathSmoke", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SmokeParticleBehavior>(), 256);
	handles_.confetti = particleManager->CreateParticleGroup(
		"DirectXGame.Confetti", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<ConfettiParticleBehavior>(), 384);
	TrailParticleBehavior::Settings suicideTrailSettings{};
	suicideTrailSettings.lifetime = 0.48f;
	suicideTrailSettings.shrinkRate = 0.94f;
	suicideTrailSettings.fadeInRatio = 0.04f;
	suicideTrailSettings.fadeOutPower = 2.2f;
	handles_.suicideEnemyTrail = particleManager->CreateParticleGroup(
		"DirectXGame.SuicideEnemyTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.82f, 0.78f, 0.76f, 0.62f }, suicideTrailSettings), 384);
	TrailParticleBehavior::Settings flameTrailSettings{};
	flameTrailSettings.lifetime = 0.30f;
	flameTrailSettings.shrinkRate = 0.90f;
	flameTrailSettings.fadeInRatio = 0.02f;
	flameTrailSettings.fadeOutPower = 1.65f;
	handles_.flameProjectileTrail = particleManager->CreateParticleGroup(
		"DirectXGame.FlameProjectileTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 1.0f, 0.34f, 0.04f, 0.78f }, flameTrailSettings), 384);
	SparkParticleBehavior::Settings flameGlowSettings{};
	flameGlowSettings.lifetime = 0.14f;
	flameGlowSettings.horizontalSpeed = 0.055f;
	flameGlowSettings.verticalSpeedMin = 0.005f;
	flameGlowSettings.verticalSpeedMax = 0.045f;
	flameGlowSettings.scaleMin = 0.16f;
	flameGlowSettings.scaleMax = 0.30f;
	flameGlowSettings.gravity = 0.0f;
	flameGlowSettings.yOffset = 0.16f;
	handles_.flameProjectileGlow = particleManager->CreateParticleGroup(
		"DirectXGame.FlameProjectileGlow", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.50f, 0.06f, 0.82f }, flameGlowSettings), 384);
	TrailParticleBehavior::Settings handgunTrailSettings{};
	handgunTrailSettings.lifetime = 0.16f;
	handgunTrailSettings.shrinkRate = 0.82f;
	handgunTrailSettings.fadeInRatio = 0.01f;
	handgunTrailSettings.fadeOutPower = 2.6f;
	handles_.handgunBulletTrail = particleManager->CreateParticleGroup(
		"DirectXGame.HandgunBulletTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 1.0f, 0.86f, 0.20f, 0.68f }, handgunTrailSettings), 384);
	TrailParticleBehavior::Settings boomerangTrailSettings{};
	boomerangTrailSettings.lifetime = 0.38f;
	boomerangTrailSettings.shrinkRate = 0.92f;
	boomerangTrailSettings.fadeInRatio = 0.03f;
	boomerangTrailSettings.fadeOutPower = 2.0f;
	handles_.boomerangTrail = particleManager->CreateParticleGroup(
		"DirectXGame.BoomerangTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.42f, 1.0f, 0.64f, 0.66f }, boomerangTrailSettings), 384);
	TrailParticleBehavior::Settings rockTrailSettings{};
	rockTrailSettings.lifetime = 0.22f;
	rockTrailSettings.shrinkRate = 0.88f;
	rockTrailSettings.fadeInRatio = 0.04f;
	rockTrailSettings.fadeOutPower = 2.3f;
	handles_.rockTrail = particleManager->CreateParticleGroup(
		"DirectXGame.RockTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.50f, 0.40f, 0.30f, 0.46f }, rockTrailSettings), 384);
	SparkParticleBehavior::Settings muzzleFlashSettings{};
	muzzleFlashSettings.lifetime = 0.095f;
	muzzleFlashSettings.horizontalSpeed = 0.075f;
	muzzleFlashSettings.verticalSpeedMin = 0.005f;
	muzzleFlashSettings.verticalSpeedMax = 0.035f;
	muzzleFlashSettings.scaleMin = 0.24f;
	muzzleFlashSettings.scaleMax = 0.42f;
	muzzleFlashSettings.gravity = 0.0f;
	muzzleFlashSettings.yOffset = 0.0f;
	handles_.handgunMuzzleFlash = particleManager->CreateParticleGroup(
		"DirectXGame.HandgunMuzzleFlash", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.92f, 0.22f, 0.92f }, muzzleFlashSettings), 256);
	SmokeParticleBehavior::Settings reloadSmokeSettings{};
	reloadSmokeSettings.lifetime = 0.28f;
	reloadSmokeSettings.offsetRange = 0.18f;
	reloadSmokeSettings.horizontalSpeed = 0.018f;
	reloadSmokeSettings.verticalSpeedMin = 0.020f;
	reloadSmokeSettings.verticalSpeedMax = 0.045f;
	reloadSmokeSettings.scaleMin = 0.20f;
	reloadSmokeSettings.scaleMax = 0.38f;
	reloadSmokeSettings.scaleGrow = 0.006f;
	reloadSmokeSettings.yOffset = 0.0f;
	handles_.handgunReloadSmoke = particleManager->CreateParticleGroup(
		"DirectXGame.HandgunReloadSmoke", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SmokeParticleBehavior>(
			Vector4{ 0.72f, 0.70f, 0.64f, 0.58f }, reloadSmokeSettings), 128);

	initialized_ = true;
	ApplyTuning();
}

void GameParticleEffects::LoadTuning(const UILayoutIO::LayoutMap& values)
{
	auto loadCount = [&values](const char* key, int32_t fallback) {
		return static_cast<int32_t>(
			UILayoutIO::GetFloat(values, key, static_cast<float>(fallback)));
	};
	tuning_.playerDamageSparkCount = loadCount(
		"particle.playerDamageSparkCount", tuning_.playerDamageSparkCount);
	tuning_.playerDamageRippleCount = loadCount(
		"particle.playerDamageRippleCount", tuning_.playerDamageRippleCount);
	tuning_.enemyHitSparkCount = loadCount(
		"particle.enemyHitSparkCount", tuning_.enemyHitSparkCount);
	tuning_.enemyDeathSparkCount = loadCount(
		"particle.enemyDeathSparkCount", tuning_.enemyDeathSparkCount);
	tuning_.enemyDeathSmokeCount = loadCount(
		"particle.enemyDeathSmokeCount", tuning_.enemyDeathSmokeCount);
	tuning_.expSparkCount = loadCount("particle.expSparkCount", tuning_.expSparkCount);
	tuning_.lightningSparkCount = loadCount(
		"particle.lightningSparkCount", tuning_.lightningSparkCount);
	tuning_.explosionBurstCount = loadCount(
		"particle.explosionBurstCount", tuning_.explosionBurstCount);
	tuning_.explosionSmokeCount = loadCount(
		"particle.explosionSmokeCount", tuning_.explosionSmokeCount);
	tuning_.levelUpConfettiCount = loadCount(
		"particle.levelUpConfettiCount", tuning_.levelUpConfettiCount);
	tuning_.playerDeathSparkCount = loadCount(
		"particle.playerDeathSparkCount", tuning_.playerDeathSparkCount);
	tuning_.playerDeathSmokeCount = loadCount(
		"particle.playerDeathSmokeCount", tuning_.playerDeathSmokeCount);
	tuning_.playerDeathRippleCount = loadCount(
		"particle.playerDeathRippleCount", tuning_.playerDeathRippleCount);
	tuning_.sparkLifetime = UILayoutIO::GetFloat(
		values, "particle.sparkLifetime", tuning_.sparkLifetime);
	tuning_.sparkVelocityScale = UILayoutIO::GetFloat(
		values, "particle.sparkVelocityScale", tuning_.sparkVelocityScale);
	tuning_.sparkScaleMultiplier = UILayoutIO::GetFloat(
		values, "particle.sparkScaleMultiplier", tuning_.sparkScaleMultiplier);
	tuning_.smokeLifetime = UILayoutIO::GetFloat(
		values, "particle.smokeLifetime", tuning_.smokeLifetime);
	tuning_.smokeScaleMultiplier = UILayoutIO::GetFloat(
		values, "particle.smokeScaleMultiplier", tuning_.smokeScaleMultiplier);
	tuning_.rippleLifetime = UILayoutIO::GetFloat(
		values, "particle.rippleLifetime", tuning_.rippleLifetime);
	tuning_.rippleExpandSpeed = UILayoutIO::GetFloat(
		values, "particle.rippleExpandSpeed", tuning_.rippleExpandSpeed);
	tuning_.confettiVelocityScale = UILayoutIO::GetFloat(
		values, "particle.confettiVelocityScale", tuning_.confettiVelocityScale);
	tuning_.confettiScaleMultiplier = UILayoutIO::GetFloat(
		values, "particle.confettiScaleMultiplier", tuning_.confettiScaleMultiplier);
	if (initialized_) {
		ApplyTuning();
	}
}

void GameParticleEffects::AppendTuningEntries(
	std::vector<UILayoutIO::Entry>& entries) const
{
	entries.insert(entries.end(), {
		{ "particle.playerDamageSparkCount", { static_cast<float>(tuning_.playerDamageSparkCount) } },
		{ "particle.playerDamageRippleCount", { static_cast<float>(tuning_.playerDamageRippleCount) } },
		{ "particle.enemyHitSparkCount", { static_cast<float>(tuning_.enemyHitSparkCount) } },
		{ "particle.enemyDeathSparkCount", { static_cast<float>(tuning_.enemyDeathSparkCount) } },
		{ "particle.enemyDeathSmokeCount", { static_cast<float>(tuning_.enemyDeathSmokeCount) } },
		{ "particle.expSparkCount", { static_cast<float>(tuning_.expSparkCount) } },
		{ "particle.lightningSparkCount", { static_cast<float>(tuning_.lightningSparkCount) } },
		{ "particle.explosionBurstCount", { static_cast<float>(tuning_.explosionBurstCount) } },
		{ "particle.explosionSmokeCount", { static_cast<float>(tuning_.explosionSmokeCount) } },
		{ "particle.levelUpConfettiCount", { static_cast<float>(tuning_.levelUpConfettiCount) } },
		{ "particle.playerDeathSparkCount", { static_cast<float>(tuning_.playerDeathSparkCount) } },
		{ "particle.playerDeathSmokeCount", { static_cast<float>(tuning_.playerDeathSmokeCount) } },
		{ "particle.playerDeathRippleCount", { static_cast<float>(tuning_.playerDeathRippleCount) } },
		{ "particle.sparkLifetime", { tuning_.sparkLifetime } },
		{ "particle.sparkVelocityScale", { tuning_.sparkVelocityScale } },
		{ "particle.sparkScaleMultiplier", { tuning_.sparkScaleMultiplier } },
		{ "particle.smokeLifetime", { tuning_.smokeLifetime } },
		{ "particle.smokeScaleMultiplier", { tuning_.smokeScaleMultiplier } },
		{ "particle.rippleLifetime", { tuning_.rippleLifetime } },
		{ "particle.rippleExpandSpeed", { tuning_.rippleExpandSpeed } },
		{ "particle.confettiVelocityScale", { tuning_.confettiVelocityScale } },
		{ "particle.confettiScaleMultiplier", { tuning_.confettiScaleMultiplier } },
	});
}

void GameParticleEffects::ApplyTuning() const
{
	if (!initialized_) {
		return;
	}

	SparkParticleBehavior::Settings sparkSettings{};
	sparkSettings.lifetime = tuning_.sparkLifetime;
	sparkSettings.horizontalSpeed *= tuning_.sparkVelocityScale;
	sparkSettings.verticalSpeedMin *= tuning_.sparkVelocityScale;
	sparkSettings.verticalSpeedMax *= tuning_.sparkVelocityScale;
	sparkSettings.scaleMin *= tuning_.sparkScaleMultiplier;
	sparkSettings.scaleMax *= tuning_.sparkScaleMultiplier;

	SmokeParticleBehavior::Settings smokeSettings{};
	smokeSettings.lifetime = tuning_.smokeLifetime;
	smokeSettings.scaleMin *= tuning_.smokeScaleMultiplier;
	smokeSettings.scaleMax *= tuning_.smokeScaleMultiplier;

	RippleParticleBehavior::Settings rippleSettings{};
	rippleSettings.lifetime = tuning_.rippleLifetime;
	rippleSettings.expandSpeed = tuning_.rippleExpandSpeed;

	ConfettiParticleBehavior::Settings confettiSettings{};
	confettiSettings.velocityScale = tuning_.confettiVelocityScale;
	confettiSettings.scaleMultiplier = tuning_.confettiScaleMultiplier;

	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	particleManager->SetBehavior(
		handles_.ripple,
		std::make_unique<RippleParticleBehavior>(
			Vector4{ 0.45f, 0.75f, 1.0f, 1.0f }, rippleSettings));
	particleManager->SetBehavior(
		handles_.spark,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.35f, 0.25f, 1.0f }, sparkSettings));
	particleManager->SetBehavior(
		handles_.enemyHitSpark,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.62f, 0.18f, 1.0f }, sparkSettings));
	particleManager->SetBehavior(
		handles_.expSpark,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 0.35f, 1.0f, 0.58f, 1.0f }, sparkSettings));
	particleManager->SetBehavior(
		handles_.playerDeathSpark,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.18f, 0.12f, 1.0f }, sparkSettings));
	ExplosionBurstParticleBehavior::Settings explosionBurstSettings{};
	explosionBurstSettings.lifetime = tuning_.sparkLifetime * 1.12f;
	explosionBurstSettings.horizontalSpeedMin *= tuning_.sparkVelocityScale;
	explosionBurstSettings.horizontalSpeedMax *= tuning_.sparkVelocityScale;
	explosionBurstSettings.verticalSpeedMin *= tuning_.sparkVelocityScale;
	explosionBurstSettings.verticalSpeedMax *= tuning_.sparkVelocityScale;
	explosionBurstSettings.scaleMin *= tuning_.sparkScaleMultiplier;
	explosionBurstSettings.scaleMax *= tuning_.sparkScaleMultiplier;
	particleManager->SetBehavior(
		handles_.explosionBurst,
		std::make_unique<ExplosionBurstParticleBehavior>(
			Vector4{ 1.0f, 0.42f, 0.05f, 1.0f },
			explosionBurstSettings));
	particleManager->SetBehavior(
		handles_.deathSmoke,
		std::make_unique<SmokeParticleBehavior>(
			Vector4{ 0.45f, 0.42f, 0.38f, 0.85f }, smokeSettings));
	SmokeParticleBehavior::Settings explosionSmokeSettings = smokeSettings;
	explosionSmokeSettings.lifetime = tuning_.smokeLifetime * 1.25f;
	explosionSmokeSettings.offsetRange = 1.4f;
	explosionSmokeSettings.scaleMin = 0.68f * tuning_.smokeScaleMultiplier;
	explosionSmokeSettings.scaleMax = 1.25f * tuning_.smokeScaleMultiplier;
	explosionSmokeSettings.scaleGrow = 0.030f;
	explosionSmokeSettings.yOffset = 0.54f;
	particleManager->SetBehavior(
		handles_.explosionSmoke,
		std::make_unique<SmokeParticleBehavior>(
			Vector4{ 0.62f, 0.42f, 0.30f, 0.72f },
			explosionSmokeSettings));
	particleManager->SetBehavior(
		handles_.confetti,
		std::make_unique<ConfettiParticleBehavior>(confettiSettings));
}

#ifdef _DEBUG
void GameParticleEffects::DrawDebugUI(const Vector3& previewPosition)
{
	ImGui::SliderInt("Damage Spark Count", &tuning_.playerDamageSparkCount, 0, 100);
	ImGui::SliderInt("Damage Ripple Count", &tuning_.playerDamageRippleCount, 0, 12);
	ImGui::SliderInt("Enemy Hit Spark Count", &tuning_.enemyHitSparkCount, 0, 80);
	ImGui::SliderInt("Enemy Death Spark Count", &tuning_.enemyDeathSparkCount, 0, 120);
	ImGui::SliderInt("Enemy Death Smoke Count", &tuning_.enemyDeathSmokeCount, 0, 80);
	ImGui::SliderInt("EXP Spark Count", &tuning_.expSparkCount, 0, 80);
	ImGui::SliderInt("Lightning Spark Count", &tuning_.lightningSparkCount, 0, 100);
	ImGui::SliderInt("Explosion Burst Count", &tuning_.explosionBurstCount, 0, 160);
	ImGui::SliderInt("Explosion Smoke Count", &tuning_.explosionSmokeCount, 0, 100);
	ImGui::SliderInt("LevelUp Confetti Count", &tuning_.levelUpConfettiCount, 0, 160);
	ImGui::SliderInt("Player Death Spark Count", &tuning_.playerDeathSparkCount, 0, 160);
	ImGui::SliderInt("Player Death Smoke Count", &tuning_.playerDeathSmokeCount, 0, 100);
	ImGui::SliderInt("Player Death Ripple Count", &tuning_.playerDeathRippleCount, 0, 12);
	bool changed = false;
	changed |= ImGui::SliderFloat("Spark Lifetime", &tuning_.sparkLifetime, 0.08f, 1.2f);
	changed |= ImGui::SliderFloat(
		"Spark Velocity Scale", &tuning_.sparkVelocityScale, 0.2f, 3.0f);
	changed |= ImGui::SliderFloat(
		"Spark Scale Multiplier", &tuning_.sparkScaleMultiplier, 0.25f, 3.0f);
	changed |= ImGui::SliderFloat("Smoke Lifetime", &tuning_.smokeLifetime, 0.2f, 2.0f);
	changed |= ImGui::SliderFloat(
		"Smoke Scale Multiplier", &tuning_.smokeScaleMultiplier, 0.25f, 3.0f);
	changed |= ImGui::SliderFloat("Ripple Lifetime", &tuning_.rippleLifetime, 0.1f, 1.2f);
	changed |= ImGui::SliderFloat(
		"Ripple Expand Speed", &tuning_.rippleExpandSpeed, 1.0f, 9.0f);
	changed |= ImGui::SliderFloat(
		"Confetti Velocity Scale", &tuning_.confettiVelocityScale, 0.2f, 3.0f);
	changed |= ImGui::SliderFloat(
		"Confetti Scale Multiplier", &tuning_.confettiScaleMultiplier, 0.25f, 3.0f);
	if (changed) {
		ApplyTuning();
	}

	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	if (ImGui::Button("Preview Damage")) {
		particleManager->Emit(
			handles_.spark, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDamageSparkCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Enemy Death")) {
		particleManager->Emit(
			handles_.enemyHitSpark, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.enemyDeathSparkCount)));
		particleManager->Emit(
			handles_.deathSmoke, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.enemyDeathSmokeCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Player Death")) {
		particleManager->Emit(
			handles_.playerDeathSpark, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDeathSparkCount)));
		particleManager->Emit(
			handles_.deathSmoke, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDeathSmokeCount)));
		particleManager->Emit(
			handles_.ripple, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDeathRippleCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Explosion")) {
		particleManager->Emit(
			handles_.explosionBurst, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.explosionBurstCount)));
		particleManager->Emit(
			handles_.explosionSmoke, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.explosionSmokeCount)));
		particleManager->Emit(handles_.ripple, previewPosition, 4u);
	}
}
#endif

}
