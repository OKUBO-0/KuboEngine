#include "GameParticleEffects.h"
#include "DataPaths.h"
#include "ParticleBehaviors.h"
#include "ParticleManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

namespace {

struct EmissionLimitSpec {
	const char* key;
	Engine::Particle::ParticleGroupHandle GameParticleEffects::Handles::* handle;
	uint32_t defaultLimit;
};

struct TrailBehaviorSpec {
	const char* key;
	Engine::Particle::ParticleGroupHandle GameParticleEffects::Handles::* handle;
	GameParticleEffects::TrailBehaviorTuning defaults;
};

struct SparkBehaviorSpec {
	const char* key;
	Engine::Particle::ParticleGroupHandle GameParticleEffects::Handles::* handle;
	GameParticleEffects::SparkBehaviorTuning defaults;
};

struct SmokeBehaviorSpec {
	const char* key;
	Engine::Particle::ParticleGroupHandle GameParticleEffects::Handles::* handle;
	GameParticleEffects::SmokeBehaviorTuning defaults;
};

struct TrailBindingSpec {
	const char* usageKey;
	const char* defaultTrailKey;
};

struct SparkBindingSpec {
	const char* usageKey;
	const char* defaultSparkKey;
};

struct SmokeBindingSpec {
	const char* usageKey;
	const char* defaultSmokeKey;
};

constexpr std::array<EmissionLimitSpec, 24> kEmissionLimitSpecs{
	EmissionLimitSpec{ "bowArrowTrail", &GameParticleEffects::Handles::bowArrowTrail, 48 },
	EmissionLimitSpec{ "flameProjectileTrail", &GameParticleEffects::Handles::flameProjectileTrail, 36 },
	EmissionLimitSpec{ "flameProjectileGlow", &GameParticleEffects::Handles::flameProjectileGlow, 48 },
	EmissionLimitSpec{ "handgunBulletTrail", &GameParticleEffects::Handles::handgunBulletTrail, 48 },
	EmissionLimitSpec{ "boomerangTrail", &GameParticleEffects::Handles::boomerangTrail, 36 },
	EmissionLimitSpec{ "rockTrail", &GameParticleEffects::Handles::rockTrail, 32 },
	EmissionLimitSpec{ "boneTrail", &GameParticleEffects::Handles::boneTrail, 32 },
	EmissionLimitSpec{ "lightningTrail", &GameParticleEffects::Handles::lightningTrail, 32 },
	EmissionLimitSpec{ "swordSlashTrail", &GameParticleEffects::Handles::swordSlashTrail, 32 },
	EmissionLimitSpec{ "auraPulseTrail", &GameParticleEffects::Handles::auraPulseTrail, 24 },
	EmissionLimitSpec{ "auraGlow", &GameParticleEffects::Handles::auraGlow, 16 },
	EmissionLimitSpec{ "flameShoeTrail", &GameParticleEffects::Handles::flameShoeTrail, 24 },
	EmissionLimitSpec{ "flameShoeGlow", &GameParticleEffects::Handles::flameShoeGlow, 64 },
	EmissionLimitSpec{ "handgunMuzzleFlash", &GameParticleEffects::Handles::handgunMuzzleFlash, 48 },
	EmissionLimitSpec{ "handgunReloadSmoke", &GameParticleEffects::Handles::handgunReloadSmoke, 24 },
	EmissionLimitSpec{ "scratchTrail", &GameParticleEffects::Handles::scratchTrail, 64 },
	EmissionLimitSpec{ "customTrail0", &GameParticleEffects::Handles::customTrail0, 64 },
	EmissionLimitSpec{ "customTrail1", &GameParticleEffects::Handles::customTrail1, 64 },
	EmissionLimitSpec{ "customTrail2", &GameParticleEffects::Handles::customTrail2, 64 },
	EmissionLimitSpec{ "customTrail3", &GameParticleEffects::Handles::customTrail3, 64 },
	EmissionLimitSpec{ "customSpark0", &GameParticleEffects::Handles::customSpark0, 64 },
	EmissionLimitSpec{ "customSpark1", &GameParticleEffects::Handles::customSpark1, 64 },
	EmissionLimitSpec{ "customSmoke0", &GameParticleEffects::Handles::customSmoke0, 32 },
	EmissionLimitSpec{ "customSmoke1", &GameParticleEffects::Handles::customSmoke1, 32 },
};

constexpr std::array<TrailBehaviorSpec, 16> kTrailBehaviorSpecs{
	TrailBehaviorSpec{
		"suicideEnemyTrail", &GameParticleEffects::Handles::suicideEnemyTrail,
		{ 0.48f, 0.94f, 0.04f, 2.2f, 0.82f, 0.78f, 0.76f, 0.62f } },
	TrailBehaviorSpec{
		"bowArrowTrail", &GameParticleEffects::Handles::bowArrowTrail,
		{ 0.24f, 0.88f, 0.03f, 2.1f, 0.58f, 0.88f, 1.0f, 0.62f } },
	TrailBehaviorSpec{
		"flameProjectileTrail", &GameParticleEffects::Handles::flameProjectileTrail,
		{ 0.30f, 0.90f, 0.02f, 1.65f, 1.0f, 0.34f, 0.04f, 0.78f } },
	TrailBehaviorSpec{
		"handgunBulletTrail", &GameParticleEffects::Handles::handgunBulletTrail,
		{ 0.16f, 0.82f, 0.01f, 2.6f, 1.0f, 0.86f, 0.20f, 0.68f } },
	TrailBehaviorSpec{
		"boomerangTrail", &GameParticleEffects::Handles::boomerangTrail,
		{ 0.38f, 0.92f, 0.03f, 2.0f, 0.42f, 1.0f, 0.64f, 0.66f } },
	TrailBehaviorSpec{
		"rockTrail", &GameParticleEffects::Handles::rockTrail,
		{ 0.22f, 0.88f, 0.04f, 2.3f, 0.50f, 0.40f, 0.30f, 0.46f } },
	TrailBehaviorSpec{
		"boneTrail", &GameParticleEffects::Handles::boneTrail,
		{ 0.26f, 0.90f, 0.02f, 2.1f, 0.90f, 0.86f, 0.72f, 0.62f } },
	TrailBehaviorSpec{
		"lightningTrail", &GameParticleEffects::Handles::lightningTrail,
		{ 0.13f, 0.76f, 0.0f, 3.1f, 0.50f, 0.90f, 1.0f, 0.72f } },
	TrailBehaviorSpec{
		"swordSlashTrail", &GameParticleEffects::Handles::swordSlashTrail,
		{ 0.18f, 0.80f, 0.0f, 2.4f, 0.92f, 0.98f, 1.0f, 0.66f } },
	TrailBehaviorSpec{
		"auraPulseTrail", &GameParticleEffects::Handles::auraPulseTrail,
		{ 0.28f, 0.90f, 0.02f, 2.0f, 0.42f, 0.86f, 1.0f, 0.54f } },
	TrailBehaviorSpec{
		"flameShoeTrail", &GameParticleEffects::Handles::flameShoeTrail,
		{ 0.34f, 0.90f, 0.02f, 1.55f, 1.0f, 0.28f, 0.02f, 0.78f } },
	TrailBehaviorSpec{
		"scratchTrail", &GameParticleEffects::Handles::scratchTrail,
		{ 0.28f, 0.88f, 0.02f, 2.0f, 0.58f, 0.90f, 1.0f, 0.68f } },
	TrailBehaviorSpec{
		"customTrail0", &GameParticleEffects::Handles::customTrail0,
		{ 0.28f, 0.88f, 0.02f, 2.0f, 0.58f, 0.90f, 1.0f, 0.68f } },
	TrailBehaviorSpec{
		"customTrail1", &GameParticleEffects::Handles::customTrail1,
		{ 0.30f, 0.90f, 0.02f, 1.8f, 1.0f, 0.44f, 0.10f, 0.72f } },
	TrailBehaviorSpec{
		"customTrail2", &GameParticleEffects::Handles::customTrail2,
		{ 0.22f, 0.82f, 0.01f, 2.8f, 0.64f, 1.0f, 0.58f, 0.62f } },
	TrailBehaviorSpec{
		"customTrail3", &GameParticleEffects::Handles::customTrail3,
		{ 0.18f, 0.78f, 0.0f, 3.0f, 1.0f, 0.92f, 0.32f, 0.66f } },
};

constexpr std::array<SparkBehaviorSpec, 10> kSparkBehaviorSpecs{
	SparkBehaviorSpec{
		"spark", &GameParticleEffects::Handles::spark,
		{ 0.35f, 0.18f, 0.06f, 0.22f, 0.18f, 0.34f, 0.012f, 0.8f, 1.0f, 0.35f, 0.25f, 1.0f } },
	SparkBehaviorSpec{
		"enemyHitSpark", &GameParticleEffects::Handles::enemyHitSpark,
		{ 0.35f, 0.18f, 0.06f, 0.22f, 0.18f, 0.34f, 0.012f, 0.8f, 1.0f, 0.62f, 0.18f, 1.0f } },
	SparkBehaviorSpec{
		"expSpark", &GameParticleEffects::Handles::expSpark,
		{ 0.35f, 0.18f, 0.06f, 0.22f, 0.18f, 0.34f, 0.012f, 0.8f, 0.35f, 1.0f, 0.58f, 1.0f } },
	SparkBehaviorSpec{
		"playerDeathSpark", &GameParticleEffects::Handles::playerDeathSpark,
		{ 0.35f, 0.18f, 0.06f, 0.22f, 0.18f, 0.34f, 0.012f, 0.8f, 1.0f, 0.18f, 0.12f, 1.0f } },
	SparkBehaviorSpec{
		"flameProjectileGlow", &GameParticleEffects::Handles::flameProjectileGlow,
		{ 0.14f, 0.055f, 0.005f, 0.045f, 0.16f, 0.30f, 0.0f, 0.16f, 1.0f, 0.50f, 0.06f, 0.82f } },
	SparkBehaviorSpec{
		"auraGlow", &GameParticleEffects::Handles::auraGlow,
		{ 0.22f, 0.035f, 0.018f, 0.075f, 0.22f, 0.48f, -0.002f, 0.34f, 0.58f, 0.92f, 1.0f, 0.58f } },
	SparkBehaviorSpec{
		"flameShoeGlow", &GameParticleEffects::Handles::flameShoeGlow,
		{ 0.30f, 0.070f, 0.060f, 0.180f, 0.28f, 0.62f, 0.006f, 0.18f, 1.0f, 0.46f, 0.05f, 0.82f } },
	SparkBehaviorSpec{
		"handgunMuzzleFlash", &GameParticleEffects::Handles::handgunMuzzleFlash,
		{ 0.095f, 0.075f, 0.005f, 0.035f, 0.24f, 0.42f, 0.0f, 0.0f, 1.0f, 0.92f, 0.22f, 0.92f } },
	SparkBehaviorSpec{
		"customSpark0", &GameParticleEffects::Handles::customSpark0,
		{ 0.24f, 0.080f, 0.020f, 0.120f, 0.20f, 0.46f, 0.004f, 0.35f, 0.55f, 0.92f, 1.0f, 0.72f } },
	SparkBehaviorSpec{
		"customSpark1", &GameParticleEffects::Handles::customSpark1,
		{ 0.30f, 0.120f, 0.040f, 0.180f, 0.18f, 0.52f, 0.008f, 0.24f, 1.0f, 0.50f, 0.08f, 0.78f } },
};

constexpr std::array<TrailBindingSpec, 10> kTrailBindingSpecs{
	TrailBindingSpec{ "bowProjectile", "bowArrowTrail" },
	TrailBindingSpec{ "flameProjectile", "flameProjectileTrail" },
	TrailBindingSpec{ "handgunProjectile", "handgunBulletTrail" },
	TrailBindingSpec{ "boomerangProjectile", "boomerangTrail" },
	TrailBindingSpec{ "boneProjectile", "boneTrail" },
	TrailBindingSpec{ "rockOrbit", "rockTrail" },
	TrailBindingSpec{ "swordSlash", "swordSlashTrail" },
	TrailBindingSpec{ "auraPulse", "auraPulseTrail" },
	TrailBindingSpec{ "flameShoeSpawn", "flameShoeTrail" },
	TrailBindingSpec{ "lightningStrike", "lightningTrail" },
};

constexpr std::array<SparkBindingSpec, 13> kSparkBindingSpecs{
	SparkBindingSpec{ "playerDamage", "spark" },
	SparkBindingSpec{ "enemyHit", "enemyHitSpark" },
	SparkBindingSpec{ "enemyDeath", "enemyHitSpark" },
	SparkBindingSpec{ "expPickup", "expSpark" },
	SparkBindingSpec{ "playerDeath", "playerDeathSpark" },
	SparkBindingSpec{ "flameProjectileGlow", "flameProjectileGlow" },
	SparkBindingSpec{ "auraGlow", "auraGlow" },
	SparkBindingSpec{ "flameShoeGlow", "flameShoeGlow" },
	SparkBindingSpec{ "handgunMuzzleFlash", "handgunMuzzleFlash" },
	SparkBindingSpec{ "bowMuzzle", "customSpark0" },
	SparkBindingSpec{ "flameMuzzle", "customSpark1" },
	SparkBindingSpec{ "boneMuzzle", "enemyHitSpark" },
	SparkBindingSpec{ "boomerangMuzzle", "customSpark0" },
};

constexpr std::array<SmokeBehaviorSpec, 5> kSmokeBehaviorSpecs{
	SmokeBehaviorSpec{
		"deathSmoke", &GameParticleEffects::Handles::deathSmoke,
		{ 0.72f, 0.65f, 0.035f, 0.025f, 0.075f, 0.45f, 0.85f, 0.018f, 0.75f, 0.45f, 0.42f, 0.38f, 0.85f } },
	SmokeBehaviorSpec{
		"explosionSmoke", &GameParticleEffects::Handles::explosionSmoke,
		{ 0.92f, 1.40f, 0.035f, 0.025f, 0.075f, 0.68f, 1.25f, 0.030f, 0.54f, 0.62f, 0.42f, 0.30f, 0.72f } },
	SmokeBehaviorSpec{
		"handgunReloadSmoke", &GameParticleEffects::Handles::handgunReloadSmoke,
		{ 0.28f, 0.18f, 0.018f, 0.020f, 0.045f, 0.20f, 0.38f, 0.006f, 0.0f, 0.72f, 0.70f, 0.64f, 0.58f } },
	SmokeBehaviorSpec{
		"customSmoke0", &GameParticleEffects::Handles::customSmoke0,
		{ 0.58f, 0.52f, 0.030f, 0.018f, 0.065f, 0.36f, 0.72f, 0.014f, 0.48f, 0.52f, 0.56f, 0.60f, 0.70f } },
	SmokeBehaviorSpec{
		"customSmoke1", &GameParticleEffects::Handles::customSmoke1,
		{ 0.44f, 0.34f, 0.026f, 0.030f, 0.095f, 0.28f, 0.58f, 0.018f, 0.25f, 0.88f, 0.44f, 0.18f, 0.66f } },
};

constexpr std::array<SmokeBindingSpec, 7> kSmokeBindingSpecs{
	SmokeBindingSpec{ "enemyDeathSmoke", "deathSmoke" },
	SmokeBindingSpec{ "playerDeathSmoke", "deathSmoke" },
	SmokeBindingSpec{ "bossDeathSmoke", "deathSmoke" },
	SmokeBindingSpec{ "explosionSmoke", "explosionSmoke" },
	SmokeBindingSpec{ "handgunReloadSmoke", "handgunReloadSmoke" },
	SmokeBindingSpec{ "bossEntranceSmoke", "deathSmoke" },
	SmokeBindingSpec{ "playerDamageSmoke", "customSmoke0" },
};

std::string MakeEmissionLimitKey(const char* key)
{
	return std::string("particle.emitLimit.") + key;
}

std::string MakeTrailBindingKey(const char* usageKey)
{
	return std::string("particle.binding.trail.") + usageKey;
}

std::string MakeSparkBindingKey(const char* usageKey)
{
	return std::string("particle.binding.spark.") + usageKey;
}

std::string MakeSmokeBindingKey(const char* usageKey)
{
	return std::string("particle.binding.smoke.") + usageKey;
}

std::string MakeTrailKey(const char* key, const char* field)
{
	return std::string("particle.trail.") + key + "." + field;
}

std::string MakeSparkKey(const char* key, const char* field)
{
	return std::string("particle.spark.") + key + "." + field;
}

std::string MakeSmokeKey(const char* key, const char* field)
{
	return std::string("particle.smoke.") + key + "." + field;
}

uint32_t GetEmissionLimit(
	const GameParticleEffects::Tuning& tuning,
	const EmissionLimitSpec& spec)
{
	const auto it = tuning.emissionLimits.find(spec.key);
	if (it == tuning.emissionLimits.end()) {
		return spec.defaultLimit;
	}
	return it->second;
}

GameParticleEffects::TrailBehaviorTuning GetTrailBehaviorTuning(
	const GameParticleEffects::Tuning& tuning,
	const TrailBehaviorSpec& spec)
{
	const auto it = tuning.trailBehaviors.find(spec.key);
	if (it == tuning.trailBehaviors.end()) {
		return spec.defaults;
	}
	return it->second;
}

std::unique_ptr<TrailParticleBehavior> MakeTrailBehavior(
	const GameParticleEffects::TrailBehaviorTuning& tuning)
{
	TrailParticleBehavior::Settings settings{};
	settings.lifetime = std::clamp(tuning.lifetime, 0.04f, 1.5f);
	settings.shrinkRate = std::clamp(tuning.shrinkRate, 0.50f, 1.0f);
	settings.fadeInRatio = std::clamp(tuning.fadeInRatio, 0.0f, 0.4f);
	settings.fadeOutPower = std::clamp(tuning.fadeOutPower, 0.2f, 5.0f);
	return std::make_unique<TrailParticleBehavior>(
		Vector4{
			std::clamp(tuning.colorR, 0.0f, 2.0f),
			std::clamp(tuning.colorG, 0.0f, 2.0f),
			std::clamp(tuning.colorB, 0.0f, 2.0f),
			std::clamp(tuning.alpha, 0.0f, 1.0f),
		},
		settings);
}

GameParticleEffects::SparkBehaviorTuning GetSparkBehaviorTuning(
	const GameParticleEffects::Tuning& tuning,
	const SparkBehaviorSpec& spec)
{
	const auto it = tuning.sparkBehaviors.find(spec.key);
	if (it == tuning.sparkBehaviors.end()) {
		return spec.defaults;
	}
	return it->second;
}

std::unique_ptr<SparkParticleBehavior> MakeSparkBehavior(
	const GameParticleEffects::SparkBehaviorTuning& tuning)
{
	SparkParticleBehavior::Settings settings{};
	settings.lifetime = std::clamp(tuning.lifetime, 0.03f, 1.5f);
	settings.horizontalSpeed = std::clamp(tuning.horizontalSpeed, 0.0f, 1.0f);
	settings.verticalSpeedMin = std::clamp(tuning.verticalSpeedMin, -0.5f, 1.0f);
	settings.verticalSpeedMax = std::clamp(tuning.verticalSpeedMax, -0.5f, 1.0f);
	settings.scaleMin = std::clamp(tuning.scaleMin, 0.02f, 2.0f);
	settings.scaleMax = std::clamp(tuning.scaleMax, 0.02f, 2.0f);
	settings.gravity = std::clamp(tuning.gravity, -0.05f, 0.08f);
	settings.yOffset = std::clamp(tuning.yOffset, -2.0f, 4.0f);
	return std::make_unique<SparkParticleBehavior>(
		Vector4{
			std::clamp(tuning.colorR, 0.0f, 2.0f),
			std::clamp(tuning.colorG, 0.0f, 2.0f),
			std::clamp(tuning.colorB, 0.0f, 2.0f),
			std::clamp(tuning.alpha, 0.0f, 1.0f),
		},
		settings);
}

GameParticleEffects::SmokeBehaviorTuning GetSmokeBehaviorTuning(
	const GameParticleEffects::Tuning& tuning,
	const SmokeBehaviorSpec& spec)
{
	const auto it = tuning.smokeBehaviors.find(spec.key);
	if (it == tuning.smokeBehaviors.end()) {
		return spec.defaults;
	}
	return it->second;
}

std::unique_ptr<SmokeParticleBehavior> MakeSmokeBehavior(
	const GameParticleEffects::SmokeBehaviorTuning& tuning)
{
	SmokeParticleBehavior::Settings settings{};
	settings.lifetime = std::clamp(tuning.lifetime, 0.08f, 2.5f);
	settings.offsetRange = std::clamp(tuning.offsetRange, 0.0f, 3.0f);
	settings.horizontalSpeed = std::clamp(tuning.horizontalSpeed, 0.0f, 0.5f);
	settings.verticalSpeedMin = std::clamp(tuning.verticalSpeedMin, -0.2f, 0.5f);
	settings.verticalSpeedMax = std::clamp(tuning.verticalSpeedMax, -0.2f, 0.8f);
	settings.scaleMin = std::clamp(tuning.scaleMin, 0.02f, 3.0f);
	settings.scaleMax = std::clamp(tuning.scaleMax, 0.02f, 3.5f);
	settings.scaleGrow = std::clamp(tuning.scaleGrow, -0.02f, 0.12f);
	settings.yOffset = std::clamp(tuning.yOffset, -1.0f, 3.0f);
	return std::make_unique<SmokeParticleBehavior>(
		Vector4{
			std::clamp(tuning.colorR, 0.0f, 2.0f),
			std::clamp(tuning.colorG, 0.0f, 2.0f),
			std::clamp(tuning.colorB, 0.0f, 2.0f),
			std::clamp(tuning.alpha, 0.0f, 1.0f),
		},
		settings);
}

const TrailBehaviorSpec* FindTrailBehaviorSpec(const std::string& key)
{
	for (const TrailBehaviorSpec& spec : kTrailBehaviorSpecs) {
		if (key == spec.key) {
			return &spec;
		}
	}
	return nullptr;
}

const SmokeBehaviorSpec* FindSmokeBehaviorSpec(const std::string& key)
{
	for (const SmokeBehaviorSpec& spec : kSmokeBehaviorSpecs) {
		if (key == spec.key) {
			return &spec;
		}
	}
	return nullptr;
}

int32_t FindTrailBehaviorIndex(const std::string& key)
{
	for (int32_t index = 0;
		index < static_cast<int32_t>(kTrailBehaviorSpecs.size());
		++index) {
		if (key == kTrailBehaviorSpecs[static_cast<size_t>(index)].key) {
			return index;
		}
	}
	return 0;
}

int32_t FindSparkBehaviorIndex(const std::string& key)
{
	for (int32_t index = 0;
		index < static_cast<int32_t>(kSparkBehaviorSpecs.size());
		++index) {
		if (key == kSparkBehaviorSpecs[static_cast<size_t>(index)].key) {
			return index;
		}
	}
	return 0;
}

int32_t FindSmokeBehaviorIndex(const std::string& key)
{
	for (int32_t index = 0;
		index < static_cast<int32_t>(kSmokeBehaviorSpecs.size());
		++index) {
		if (key == kSmokeBehaviorSpecs[static_cast<size_t>(index)].key) {
			return index;
		}
	}
	return 0;
}

const SparkBehaviorSpec* FindSparkBehaviorSpec(const std::string& key)
{
	for (const SparkBehaviorSpec& spec : kSparkBehaviorSpecs) {
		if (key == spec.key) {
			return &spec;
		}
	}
	return nullptr;
}

std::string GetTrailBinding(
	const GameParticleEffects::Tuning& tuning,
	const TrailBindingSpec& spec)
{
	const auto it = tuning.trailBindings.find(spec.usageKey);
	if (it == tuning.trailBindings.end() ||
		!FindTrailBehaviorSpec(it->second)) {
		return spec.defaultTrailKey;
	}
	return it->second;
}

std::string GetSparkBinding(
	const GameParticleEffects::Tuning& tuning,
	const SparkBindingSpec& spec)
{
	const auto it = tuning.sparkBindings.find(spec.usageKey);
	if (it == tuning.sparkBindings.end() ||
		!FindSparkBehaviorSpec(it->second)) {
		return spec.defaultSparkKey;
	}
	return it->second;
}

std::string GetSmokeBinding(
	const GameParticleEffects::Tuning& tuning,
	const SmokeBindingSpec& spec)
{
	const auto it = tuning.smokeBindings.find(spec.usageKey);
	if (it == tuning.smokeBindings.end() ||
		!FindSmokeBehaviorSpec(it->second)) {
		return spec.defaultSmokeKey;
	}
	return it->second;
}

} // namespace

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
	TrailParticleBehavior::Settings bowTrailSettings{};
	bowTrailSettings.lifetime = 0.18f;
	bowTrailSettings.shrinkRate = 0.88f;
	bowTrailSettings.fadeInRatio = 0.03f;
	bowTrailSettings.fadeOutPower = 2.1f;
	handles_.bowArrowTrail = particleManager->CreateParticleGroup(
		"DirectXGame.BowArrowTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.58f, 0.88f, 1.0f, 0.62f }, bowTrailSettings), 512);
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
	rockTrailSettings.lifetime = 0.16f;
	rockTrailSettings.shrinkRate = 0.88f;
	rockTrailSettings.fadeInRatio = 0.04f;
	rockTrailSettings.fadeOutPower = 2.3f;
	handles_.rockTrail = particleManager->CreateParticleGroup(
		"DirectXGame.RockTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.50f, 0.40f, 0.30f, 0.46f }, rockTrailSettings), 384);
	TrailParticleBehavior::Settings boneTrailSettings{};
	boneTrailSettings.lifetime = 0.18f;
	boneTrailSettings.shrinkRate = 0.90f;
	boneTrailSettings.fadeInRatio = 0.02f;
	boneTrailSettings.fadeOutPower = 2.1f;
	handles_.boneTrail = particleManager->CreateParticleGroup(
		"DirectXGame.BoneTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.90f, 0.86f, 0.72f, 0.62f }, boneTrailSettings), 384);
	TrailParticleBehavior::Settings lightningTrailSettings{};
	lightningTrailSettings.lifetime = 0.13f;
	lightningTrailSettings.shrinkRate = 0.76f;
	lightningTrailSettings.fadeInRatio = 0.00f;
	lightningTrailSettings.fadeOutPower = 3.1f;
	handles_.lightningTrail = particleManager->CreateParticleGroup(
		"DirectXGame.LightningTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.50f, 0.90f, 1.0f, 0.72f }, lightningTrailSettings), 256);
	TrailParticleBehavior::Settings swordTrailSettings{};
	swordTrailSettings.lifetime = 0.18f;
	swordTrailSettings.shrinkRate = 0.80f;
	swordTrailSettings.fadeInRatio = 0.00f;
	swordTrailSettings.fadeOutPower = 2.4f;
	handles_.swordSlashTrail = particleManager->CreateParticleGroup(
		"DirectXGame.SwordSlashTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.92f, 0.98f, 1.0f, 0.66f }, swordTrailSettings), 384);
	TrailParticleBehavior::Settings auraTrailSettings{};
	auraTrailSettings.lifetime = 0.28f;
	auraTrailSettings.shrinkRate = 0.90f;
	auraTrailSettings.fadeInRatio = 0.02f;
	auraTrailSettings.fadeOutPower = 2.0f;
	handles_.auraPulseTrail = particleManager->CreateParticleGroup(
		"DirectXGame.AuraPulseTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 0.42f, 0.86f, 1.0f, 0.54f }, auraTrailSettings), 384);
	SparkParticleBehavior::Settings auraGlowSettings{};
	auraGlowSettings.lifetime = 0.22f;
	auraGlowSettings.horizontalSpeed = 0.035f;
	auraGlowSettings.verticalSpeedMin = 0.018f;
	auraGlowSettings.verticalSpeedMax = 0.075f;
	auraGlowSettings.scaleMin = 0.22f;
	auraGlowSettings.scaleMax = 0.48f;
	auraGlowSettings.gravity = -0.002f;
	auraGlowSettings.yOffset = 0.34f;
	handles_.auraGlow = particleManager->CreateParticleGroup(
		"DirectXGame.AuraGlow", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 0.58f, 0.92f, 1.0f, 0.58f }, auraGlowSettings), 256);
	TrailParticleBehavior::Settings flameShoeTrailSettings{};
	flameShoeTrailSettings.lifetime = 0.34f;
	flameShoeTrailSettings.shrinkRate = 0.90f;
	flameShoeTrailSettings.fadeInRatio = 0.02f;
	flameShoeTrailSettings.fadeOutPower = 1.55f;
	handles_.flameShoeTrail = particleManager->CreateParticleGroup(
		"DirectXGame.FlameShoeTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<TrailParticleBehavior>(
			Vector4{ 1.0f, 0.28f, 0.02f, 0.78f }, flameShoeTrailSettings), 384);
	SparkParticleBehavior::Settings flameShoeGlowSettings{};
	flameShoeGlowSettings.lifetime = 0.30f;
	flameShoeGlowSettings.horizontalSpeed = 0.070f;
	flameShoeGlowSettings.verticalSpeedMin = 0.060f;
	flameShoeGlowSettings.verticalSpeedMax = 0.180f;
	flameShoeGlowSettings.scaleMin = 0.28f;
	flameShoeGlowSettings.scaleMax = 0.62f;
	flameShoeGlowSettings.gravity = 0.006f;
	flameShoeGlowSettings.yOffset = 0.18f;
	handles_.flameShoeGlow = particleManager->CreateParticleGroup(
		"DirectXGame.FlameShoeGlow", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(
			Vector4{ 1.0f, 0.46f, 0.05f, 0.82f }, flameShoeGlowSettings), 384);
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
	handles_.scratchTrail = particleManager->CreateParticleGroup(
		"DirectXGame.ScratchTrail", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, kTrailBehaviorSpecs[11])),
		384);
	handles_.customTrail0 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomTrail0", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, kTrailBehaviorSpecs[12])),
		384);
	handles_.customTrail1 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomTrail1", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, kTrailBehaviorSpecs[13])),
		384);
	handles_.customTrail2 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomTrail2", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, kTrailBehaviorSpecs[14])),
		384);
	handles_.customTrail3 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomTrail3", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, kTrailBehaviorSpecs[15])),
		384);
	handles_.customSpark0 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomSpark0", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeSparkBehavior(GetSparkBehaviorTuning(tuning_, kSparkBehaviorSpecs[8])),
		384);
	handles_.customSpark1 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomSpark1", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeSparkBehavior(GetSparkBehaviorTuning(tuning_, kSparkBehaviorSpecs[9])),
		384);
	handles_.customSmoke0 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomSmoke0", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeSmokeBehavior(GetSmokeBehaviorTuning(tuning_, kSmokeBehaviorSpecs[3])),
		192);
	handles_.customSmoke1 = particleManager->CreateParticleGroup(
		"DirectXGame.CustomSmoke1", "Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		MakeSmokeBehavior(GetSmokeBehaviorTuning(tuning_, kSmokeBehaviorSpecs[4])),
		192);

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
	tuning_.particleEmissionScale = UILayoutIO::GetFloat(
		values, "particle.emissionScale", tuning_.particleEmissionScale);
	tuning_.bowTrailWidth = UILayoutIO::GetFloat(
		values, "particle.bowTrailWidth", tuning_.bowTrailWidth);
	tuning_.bowTrailLengthMultiplier = UILayoutIO::GetFloat(
		values, "particle.bowTrailLengthMultiplier", tuning_.bowTrailLengthMultiplier);
	tuning_.bowTrailMaxFrameDistance = UILayoutIO::GetFloat(
		values, "particle.bowTrailMaxFrameDistance", tuning_.bowTrailMaxFrameDistance);
	tuning_.bowTrailLifetime = UILayoutIO::GetFloat(
		values, "particle.bowTrailLifetime", tuning_.bowTrailLifetime);
	tuning_.bowTrailShrinkRate = UILayoutIO::GetFloat(
		values, "particle.bowTrailShrinkRate", tuning_.bowTrailShrinkRate);
	tuning_.bowTrailFadeInRatio = UILayoutIO::GetFloat(
		values, "particle.bowTrailFadeInRatio", tuning_.bowTrailFadeInRatio);
	tuning_.bowTrailFadeOutPower = UILayoutIO::GetFloat(
		values, "particle.bowTrailFadeOutPower", tuning_.bowTrailFadeOutPower);
	tuning_.bowTrailColorR = UILayoutIO::GetFloat(
		values, "particle.bowTrailColorR", tuning_.bowTrailColorR);
	tuning_.bowTrailColorG = UILayoutIO::GetFloat(
		values, "particle.bowTrailColorG", tuning_.bowTrailColorG);
	tuning_.bowTrailColorB = UILayoutIO::GetFloat(
		values, "particle.bowTrailColorB", tuning_.bowTrailColorB);
	tuning_.bowTrailAlpha = UILayoutIO::GetFloat(
		values, "particle.bowTrailAlpha", tuning_.bowTrailAlpha);
	tuning_.trailBehaviors["bowArrowTrail"] = {
		tuning_.bowTrailLifetime,
		tuning_.bowTrailShrinkRate,
		tuning_.bowTrailFadeInRatio,
		tuning_.bowTrailFadeOutPower,
		tuning_.bowTrailColorR,
		tuning_.bowTrailColorG,
		tuning_.bowTrailColorB,
		tuning_.bowTrailAlpha,
	};
	for (const EmissionLimitSpec& spec : kEmissionLimitSpecs) {
		tuning_.emissionLimits[spec.key] = static_cast<uint32_t>(
			(std::max)(
				0,
				loadCount(
					MakeEmissionLimitKey(spec.key).c_str(),
					static_cast<int32_t>(GetEmissionLimit(tuning_, spec)))));
	}
	for (const TrailBehaviorSpec& spec : kTrailBehaviorSpecs) {
		GameParticleEffects::TrailBehaviorTuning trailTuning =
			GetTrailBehaviorTuning(tuning_, spec);
		trailTuning.lifetime = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "lifetime"),
			trailTuning.lifetime);
		trailTuning.shrinkRate = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "shrinkRate"),
			trailTuning.shrinkRate);
		trailTuning.fadeInRatio = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "fadeInRatio"),
			trailTuning.fadeInRatio);
		trailTuning.fadeOutPower = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "fadeOutPower"),
			trailTuning.fadeOutPower);
		trailTuning.colorR = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "colorR"),
			trailTuning.colorR);
		trailTuning.colorG = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "colorG"),
			trailTuning.colorG);
		trailTuning.colorB = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "colorB"),
			trailTuning.colorB);
		trailTuning.alpha = UILayoutIO::GetFloat(
			values,
			MakeTrailKey(spec.key, "alpha"),
			trailTuning.alpha);
		tuning_.trailBehaviors[spec.key] = trailTuning;
	}
	for (const SparkBehaviorSpec& spec : kSparkBehaviorSpecs) {
		GameParticleEffects::SparkBehaviorTuning sparkTuning =
			GetSparkBehaviorTuning(tuning_, spec);
		sparkTuning.lifetime = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "lifetime"), sparkTuning.lifetime);
		sparkTuning.horizontalSpeed = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "horizontalSpeed"), sparkTuning.horizontalSpeed);
		sparkTuning.verticalSpeedMin = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "verticalSpeedMin"), sparkTuning.verticalSpeedMin);
		sparkTuning.verticalSpeedMax = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "verticalSpeedMax"), sparkTuning.verticalSpeedMax);
		sparkTuning.scaleMin = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "scaleMin"), sparkTuning.scaleMin);
		sparkTuning.scaleMax = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "scaleMax"), sparkTuning.scaleMax);
		sparkTuning.gravity = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "gravity"), sparkTuning.gravity);
		sparkTuning.yOffset = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "yOffset"), sparkTuning.yOffset);
		sparkTuning.colorR = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "colorR"), sparkTuning.colorR);
		sparkTuning.colorG = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "colorG"), sparkTuning.colorG);
		sparkTuning.colorB = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "colorB"), sparkTuning.colorB);
		sparkTuning.alpha = UILayoutIO::GetFloat(
			values, MakeSparkKey(spec.key, "alpha"), sparkTuning.alpha);
		tuning_.sparkBehaviors[spec.key] = sparkTuning;
	}
	for (const SmokeBehaviorSpec& spec : kSmokeBehaviorSpecs) {
		GameParticleEffects::SmokeBehaviorTuning smokeTuning =
			GetSmokeBehaviorTuning(tuning_, spec);
		smokeTuning.lifetime = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "lifetime"), smokeTuning.lifetime);
		smokeTuning.offsetRange = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "offsetRange"), smokeTuning.offsetRange);
		smokeTuning.horizontalSpeed = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "horizontalSpeed"), smokeTuning.horizontalSpeed);
		smokeTuning.verticalSpeedMin = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "verticalSpeedMin"), smokeTuning.verticalSpeedMin);
		smokeTuning.verticalSpeedMax = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "verticalSpeedMax"), smokeTuning.verticalSpeedMax);
		smokeTuning.scaleMin = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "scaleMin"), smokeTuning.scaleMin);
		smokeTuning.scaleMax = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "scaleMax"), smokeTuning.scaleMax);
		smokeTuning.scaleGrow = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "scaleGrow"), smokeTuning.scaleGrow);
		smokeTuning.yOffset = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "yOffset"), smokeTuning.yOffset);
		smokeTuning.colorR = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "colorR"), smokeTuning.colorR);
		smokeTuning.colorG = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "colorG"), smokeTuning.colorG);
		smokeTuning.colorB = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "colorB"), smokeTuning.colorB);
		smokeTuning.alpha = UILayoutIO::GetFloat(
			values, MakeSmokeKey(spec.key, "alpha"), smokeTuning.alpha);
		tuning_.smokeBehaviors[spec.key] = smokeTuning;
	}
	for (const TrailBindingSpec& spec : kTrailBindingSpecs) {
		const int32_t defaultIndex =
			FindTrailBehaviorIndex(GetTrailBinding(tuning_, spec));
		const int32_t loadedIndex = std::clamp(
			loadCount(MakeTrailBindingKey(spec.usageKey).c_str(), defaultIndex),
			0,
			static_cast<int32_t>(kTrailBehaviorSpecs.size()) - 1);
		tuning_.trailBindings[spec.usageKey] =
			kTrailBehaviorSpecs[static_cast<size_t>(loadedIndex)].key;
	}
	for (const SparkBindingSpec& spec : kSparkBindingSpecs) {
		const int32_t defaultIndex =
			FindSparkBehaviorIndex(GetSparkBinding(tuning_, spec));
		const int32_t loadedIndex = std::clamp(
			loadCount(MakeSparkBindingKey(spec.usageKey).c_str(), defaultIndex),
			0,
			static_cast<int32_t>(kSparkBehaviorSpecs.size()) - 1);
		tuning_.sparkBindings[spec.usageKey] =
			kSparkBehaviorSpecs[static_cast<size_t>(loadedIndex)].key;
	}
	for (const SmokeBindingSpec& spec : kSmokeBindingSpecs) {
		const int32_t defaultIndex =
			FindSmokeBehaviorIndex(GetSmokeBinding(tuning_, spec));
		const int32_t loadedIndex = std::clamp(
			loadCount(MakeSmokeBindingKey(spec.usageKey).c_str(), defaultIndex),
			0,
			static_cast<int32_t>(kSmokeBehaviorSpecs.size()) - 1);
		tuning_.smokeBindings[spec.usageKey] =
			kSmokeBehaviorSpecs[static_cast<size_t>(loadedIndex)].key;
	}
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
		{ "particle.emissionScale", { tuning_.particleEmissionScale } },
		{ "particle.bowTrailWidth", { tuning_.bowTrailWidth } },
		{ "particle.bowTrailLengthMultiplier", { tuning_.bowTrailLengthMultiplier } },
		{ "particle.bowTrailMaxFrameDistance", { tuning_.bowTrailMaxFrameDistance } },
		{ "particle.bowTrailLifetime", { tuning_.bowTrailLifetime } },
		{ "particle.bowTrailShrinkRate", { tuning_.bowTrailShrinkRate } },
		{ "particle.bowTrailFadeInRatio", { tuning_.bowTrailFadeInRatio } },
		{ "particle.bowTrailFadeOutPower", { tuning_.bowTrailFadeOutPower } },
		{ "particle.bowTrailColorR", { tuning_.bowTrailColorR } },
		{ "particle.bowTrailColorG", { tuning_.bowTrailColorG } },
		{ "particle.bowTrailColorB", { tuning_.bowTrailColorB } },
		{ "particle.bowTrailAlpha", { tuning_.bowTrailAlpha } },
	});
	for (const EmissionLimitSpec& spec : kEmissionLimitSpecs) {
		entries.push_back({
			MakeEmissionLimitKey(spec.key),
			{ static_cast<float>(GetEmissionLimit(tuning_, spec)) },
		});
	}
	for (const TrailBehaviorSpec& spec : kTrailBehaviorSpecs) {
		const GameParticleEffects::TrailBehaviorTuning trailTuning =
			GetTrailBehaviorTuning(tuning_, spec);
		entries.push_back({ MakeTrailKey(spec.key, "lifetime"), { trailTuning.lifetime } });
		entries.push_back({ MakeTrailKey(spec.key, "shrinkRate"), { trailTuning.shrinkRate } });
		entries.push_back({ MakeTrailKey(spec.key, "fadeInRatio"), { trailTuning.fadeInRatio } });
		entries.push_back({ MakeTrailKey(spec.key, "fadeOutPower"), { trailTuning.fadeOutPower } });
		entries.push_back({ MakeTrailKey(spec.key, "colorR"), { trailTuning.colorR } });
		entries.push_back({ MakeTrailKey(spec.key, "colorG"), { trailTuning.colorG } });
		entries.push_back({ MakeTrailKey(spec.key, "colorB"), { trailTuning.colorB } });
		entries.push_back({ MakeTrailKey(spec.key, "alpha"), { trailTuning.alpha } });
	}
	for (const SparkBehaviorSpec& spec : kSparkBehaviorSpecs) {
		const GameParticleEffects::SparkBehaviorTuning sparkTuning =
			GetSparkBehaviorTuning(tuning_, spec);
		entries.push_back({ MakeSparkKey(spec.key, "lifetime"), { sparkTuning.lifetime } });
		entries.push_back({ MakeSparkKey(spec.key, "horizontalSpeed"), { sparkTuning.horizontalSpeed } });
		entries.push_back({ MakeSparkKey(spec.key, "verticalSpeedMin"), { sparkTuning.verticalSpeedMin } });
		entries.push_back({ MakeSparkKey(spec.key, "verticalSpeedMax"), { sparkTuning.verticalSpeedMax } });
		entries.push_back({ MakeSparkKey(spec.key, "scaleMin"), { sparkTuning.scaleMin } });
		entries.push_back({ MakeSparkKey(spec.key, "scaleMax"), { sparkTuning.scaleMax } });
		entries.push_back({ MakeSparkKey(spec.key, "gravity"), { sparkTuning.gravity } });
		entries.push_back({ MakeSparkKey(spec.key, "yOffset"), { sparkTuning.yOffset } });
		entries.push_back({ MakeSparkKey(spec.key, "colorR"), { sparkTuning.colorR } });
		entries.push_back({ MakeSparkKey(spec.key, "colorG"), { sparkTuning.colorG } });
		entries.push_back({ MakeSparkKey(spec.key, "colorB"), { sparkTuning.colorB } });
		entries.push_back({ MakeSparkKey(spec.key, "alpha"), { sparkTuning.alpha } });
	}
	for (const SmokeBehaviorSpec& spec : kSmokeBehaviorSpecs) {
		const GameParticleEffects::SmokeBehaviorTuning smokeTuning =
			GetSmokeBehaviorTuning(tuning_, spec);
		entries.push_back({ MakeSmokeKey(spec.key, "lifetime"), { smokeTuning.lifetime } });
		entries.push_back({ MakeSmokeKey(spec.key, "offsetRange"), { smokeTuning.offsetRange } });
		entries.push_back({ MakeSmokeKey(spec.key, "horizontalSpeed"), { smokeTuning.horizontalSpeed } });
		entries.push_back({ MakeSmokeKey(spec.key, "verticalSpeedMin"), { smokeTuning.verticalSpeedMin } });
		entries.push_back({ MakeSmokeKey(spec.key, "verticalSpeedMax"), { smokeTuning.verticalSpeedMax } });
		entries.push_back({ MakeSmokeKey(spec.key, "scaleMin"), { smokeTuning.scaleMin } });
		entries.push_back({ MakeSmokeKey(spec.key, "scaleMax"), { smokeTuning.scaleMax } });
		entries.push_back({ MakeSmokeKey(spec.key, "scaleGrow"), { smokeTuning.scaleGrow } });
		entries.push_back({ MakeSmokeKey(spec.key, "yOffset"), { smokeTuning.yOffset } });
		entries.push_back({ MakeSmokeKey(spec.key, "colorR"), { smokeTuning.colorR } });
		entries.push_back({ MakeSmokeKey(spec.key, "colorG"), { smokeTuning.colorG } });
		entries.push_back({ MakeSmokeKey(spec.key, "colorB"), { smokeTuning.colorB } });
		entries.push_back({ MakeSmokeKey(spec.key, "alpha"), { smokeTuning.alpha } });
	}
	for (const TrailBindingSpec& spec : kTrailBindingSpecs) {
		entries.push_back({
			MakeTrailBindingKey(spec.usageKey),
			{ static_cast<float>(FindTrailBehaviorIndex(GetTrailBinding(tuning_, spec))) },
		});
	}
	for (const SparkBindingSpec& spec : kSparkBindingSpecs) {
		entries.push_back({
			MakeSparkBindingKey(spec.usageKey),
			{ static_cast<float>(FindSparkBehaviorIndex(GetSparkBinding(tuning_, spec))) },
		});
	}
	for (const SmokeBindingSpec& spec : kSmokeBindingSpecs) {
		entries.push_back({
			MakeSmokeBindingKey(spec.usageKey),
			{ static_cast<float>(FindSmokeBehaviorIndex(GetSmokeBinding(tuning_, spec))) },
		});
	}
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
	particleManager->SetGlobalEmissionScale(tuning_.particleEmissionScale);
	for (const EmissionLimitSpec& spec : kEmissionLimitSpecs) {
		particleManager->SetParticleGroupEmissionLimit(
			handles_.*(spec.handle),
			GetEmissionLimit(tuning_, spec));
	}
	for (const TrailBehaviorSpec& spec : kTrailBehaviorSpecs) {
		particleManager->SetBehavior(
			handles_.*(spec.handle),
			MakeTrailBehavior(GetTrailBehaviorTuning(tuning_, spec)));
	}
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
	for (const SparkBehaviorSpec& spec : kSparkBehaviorSpecs) {
		particleManager->SetBehavior(
			handles_.*(spec.handle),
			MakeSparkBehavior(GetSparkBehaviorTuning(tuning_, spec)));
	}
	for (const SmokeBehaviorSpec& spec : kSmokeBehaviorSpecs) {
		particleManager->SetBehavior(
			handles_.*(spec.handle),
			MakeSmokeBehavior(GetSmokeBehaviorTuning(tuning_, spec)));
	}

}

Engine::Particle::ParticleGroupHandle GameParticleEffects::GetTrailBindingHandle(
	const std::string& usageKey) const
{
	for (const TrailBindingSpec& bindingSpec : kTrailBindingSpecs) {
		if (usageKey != bindingSpec.usageKey) {
			continue;
		}
		const std::string trailKey = GetTrailBinding(tuning_, bindingSpec);
		const TrailBehaviorSpec* trailSpec = FindTrailBehaviorSpec(trailKey);
		if (!trailSpec) {
			trailSpec = FindTrailBehaviorSpec(bindingSpec.defaultTrailKey);
		}
		if (!trailSpec) {
			return {};
		}
		return handles_.*(trailSpec->handle);
	}
	return {};
}

Engine::Particle::ParticleGroupHandle GameParticleEffects::GetSparkBindingHandle(
	const std::string& usageKey) const
{
	for (const SparkBindingSpec& bindingSpec : kSparkBindingSpecs) {
		if (usageKey != bindingSpec.usageKey) {
			continue;
		}
		const std::string sparkKey = GetSparkBinding(tuning_, bindingSpec);
		const SparkBehaviorSpec* sparkSpec = FindSparkBehaviorSpec(sparkKey);
		if (!sparkSpec) {
			sparkSpec = FindSparkBehaviorSpec(bindingSpec.defaultSparkKey);
		}
		if (!sparkSpec) {
			return {};
		}
		return handles_.*(sparkSpec->handle);
	}
	return {};
}

Engine::Particle::ParticleGroupHandle GameParticleEffects::GetSmokeBindingHandle(
	const std::string& usageKey) const
{
	for (const SmokeBindingSpec& bindingSpec : kSmokeBindingSpecs) {
		if (usageKey != bindingSpec.usageKey) {
			continue;
		}
		const std::string smokeKey = GetSmokeBinding(tuning_, bindingSpec);
		const SmokeBehaviorSpec* smokeSpec = FindSmokeBehaviorSpec(smokeKey);
		if (!smokeSpec) {
			smokeSpec = FindSmokeBehaviorSpec(bindingSpec.defaultSmokeKey);
		}
		if (!smokeSpec) {
			return {};
		}
		return handles_.*(smokeSpec->handle);
	}
	return {};
}

#ifdef _DEBUG
void GameParticleEffects::DrawDebugUI(const Vector3& previewPosition)
{
	static int32_t selectedTrailIndex = 1;
	static float workshopWidth = 0.22f;
	static float workshopLength = 4.0f;
	static float workshopRadius = 2.7f;
	static int32_t workshopSegments = 18;
	static int32_t workshopCopyTargetIndex = 11;
	static int32_t selectedSparkIndex = 4;
	static int32_t workshopSparkCount = 12;
	static int32_t workshopSparkCopyTargetIndex = 8;
	static int32_t selectedSmokeIndex = 0;
	static int32_t workshopSmokeCount = 12;
	static int32_t workshopSmokeCopyTargetIndex = 3;

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
	changed |= ImGui::SliderFloat(
		"Particle Emission Scale", &tuning_.particleEmissionScale, 0.0f, 1.0f);
	if (ImGui::CollapsingHeader("Bow Arrow Trail", ImGuiTreeNodeFlags_DefaultOpen)) {
		GameParticleEffects::TrailBehaviorTuning& bowTrailTuning =
			tuning_.trailBehaviors["bowArrowTrail"];
		if (bowTrailTuning.lifetime <= 0.0f) {
			bowTrailTuning = GetTrailBehaviorTuning(
				tuning_,
				kTrailBehaviorSpecs[1]);
		}
		changed |= ImGui::SliderFloat(
			"Bow Trail Width", &tuning_.bowTrailWidth, 0.02f, 1.2f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Length", &tuning_.bowTrailLengthMultiplier, 0.2f, 6.0f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Max Step", &tuning_.bowTrailMaxFrameDistance, 0.2f, 12.0f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Lifetime", &bowTrailTuning.lifetime, 0.04f, 1.5f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Shrink", &bowTrailTuning.shrinkRate, 0.50f, 1.0f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Fade In", &bowTrailTuning.fadeInRatio, 0.0f, 0.4f);
		changed |= ImGui::SliderFloat(
			"Bow Trail Fade Out", &bowTrailTuning.fadeOutPower, 0.2f, 5.0f);
		float bowTrailColor[4]{
			bowTrailTuning.colorR,
			bowTrailTuning.colorG,
			bowTrailTuning.colorB,
			bowTrailTuning.alpha,
		};
		if (ImGui::ColorEdit4("Bow Trail Color", bowTrailColor)) {
			bowTrailTuning.colorR = bowTrailColor[0];
			bowTrailTuning.colorG = bowTrailColor[1];
			bowTrailTuning.colorB = bowTrailColor[2];
			bowTrailTuning.alpha = bowTrailColor[3];
			changed = true;
		}
	}
	if (ImGui::CollapsingHeader("Trail Behaviors")) {
		for (const TrailBehaviorSpec& spec : kTrailBehaviorSpecs) {
			GameParticleEffects::TrailBehaviorTuning& trailTuning =
				tuning_.trailBehaviors[spec.key];
			if (trailTuning.lifetime <= 0.0f) {
				trailTuning = spec.defaults;
			}
			if (!ImGui::TreeNode(spec.key)) {
				continue;
			}
			changed |= ImGui::SliderFloat(
				"Lifetime", &trailTuning.lifetime, 0.04f, 1.5f);
			changed |= ImGui::SliderFloat(
				"Shrink", &trailTuning.shrinkRate, 0.50f, 1.0f);
			changed |= ImGui::SliderFloat(
				"Fade In", &trailTuning.fadeInRatio, 0.0f, 0.4f);
			changed |= ImGui::SliderFloat(
				"Fade Out", &trailTuning.fadeOutPower, 0.2f, 5.0f);
			float color[4]{
				trailTuning.colorR,
				trailTuning.colorG,
				trailTuning.colorB,
				trailTuning.alpha,
			};
			if (ImGui::ColorEdit4("Color", color)) {
				trailTuning.colorR = color[0];
				trailTuning.colorG = color[1];
				trailTuning.colorB = color[2];
				trailTuning.alpha = color[3];
				changed = true;
			}
			ImGui::TreePop();
		}
	}
	if (ImGui::CollapsingHeader("Particle Workshop", ImGuiTreeNodeFlags_DefaultOpen)) {
		selectedTrailIndex = std::clamp(
			selectedTrailIndex,
			0,
			static_cast<int32_t>(kTrailBehaviorSpecs.size()) - 1);
		const TrailBehaviorSpec& selectedSpec =
			kTrailBehaviorSpecs[static_cast<size_t>(selectedTrailIndex)];
		if (ImGui::BeginCombo("Trail Preset", selectedSpec.key)) {
			for (int32_t index = 0;
				index < static_cast<int32_t>(kTrailBehaviorSpecs.size());
				++index) {
				const bool selected = index == selectedTrailIndex;
				if (ImGui::Selectable(
					kTrailBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					selectedTrailIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		GameParticleEffects::TrailBehaviorTuning& selectedTrail =
			tuning_.trailBehaviors[selectedSpec.key];
		if (selectedTrail.lifetime <= 0.0f) {
			selectedTrail = selectedSpec.defaults;
		}
		changed |= ImGui::SliderFloat(
			"Workshop Width", &workshopWidth, 0.02f, 1.2f);
		changed |= ImGui::SliderFloat(
			"Workshop Length", &workshopLength, 0.2f, 12.0f);
		changed |= ImGui::SliderFloat(
			"Workshop Radius", &workshopRadius, 0.2f, 8.0f);
		changed |= ImGui::SliderInt(
			"Workshop Segments", &workshopSegments, 3, 64);
		changed |= ImGui::SliderFloat(
			"Workshop Lifetime", &selectedTrail.lifetime, 0.04f, 1.5f);
		changed |= ImGui::SliderFloat(
			"Workshop Shrink", &selectedTrail.shrinkRate, 0.50f, 1.0f);
		changed |= ImGui::SliderFloat(
			"Workshop Fade In", &selectedTrail.fadeInRatio, 0.0f, 0.4f);
		changed |= ImGui::SliderFloat(
			"Workshop Fade Out", &selectedTrail.fadeOutPower, 0.2f, 5.0f);
		float workshopColor[4]{
			selectedTrail.colorR,
			selectedTrail.colorG,
			selectedTrail.colorB,
			selectedTrail.alpha,
		};
		if (ImGui::ColorEdit4("Workshop Color", workshopColor)) {
			selectedTrail.colorR = workshopColor[0];
			selectedTrail.colorG = workshopColor[1];
			selectedTrail.colorB = workshopColor[2];
			selectedTrail.alpha = workshopColor[3];
			changed = true;
		}
		workshopCopyTargetIndex = std::clamp(
			workshopCopyTargetIndex,
			11,
			static_cast<int32_t>(kTrailBehaviorSpecs.size()) - 1);
		if (ImGui::BeginCombo(
			"Copy Target",
			kTrailBehaviorSpecs[static_cast<size_t>(workshopCopyTargetIndex)].key)) {
			for (int32_t index = 11;
				index < static_cast<int32_t>(kTrailBehaviorSpecs.size());
				++index) {
				const bool selected = index == workshopCopyTargetIndex;
				if (ImGui::Selectable(
					kTrailBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					workshopCopyTargetIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Copy Selected To Target")) {
			tuning_.trailBehaviors[
				kTrailBehaviorSpecs[static_cast<size_t>(workshopCopyTargetIndex)].key] =
				selectedTrail;
			selectedTrailIndex = workshopCopyTargetIndex;
			changed = true;
		}
		ImGui::Separator();
		selectedSparkIndex = std::clamp(
			selectedSparkIndex,
			0,
			static_cast<int32_t>(kSparkBehaviorSpecs.size()) - 1);
		const SparkBehaviorSpec& selectedSparkSpec =
			kSparkBehaviorSpecs[static_cast<size_t>(selectedSparkIndex)];
		if (ImGui::BeginCombo("Spark Preset", selectedSparkSpec.key)) {
			for (int32_t index = 0;
				index < static_cast<int32_t>(kSparkBehaviorSpecs.size());
				++index) {
				const bool selected = index == selectedSparkIndex;
				if (ImGui::Selectable(
					kSparkBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					selectedSparkIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		GameParticleEffects::SparkBehaviorTuning& selectedSpark =
			tuning_.sparkBehaviors[selectedSparkSpec.key];
		if (selectedSpark.lifetime <= 0.0f) {
			selectedSpark = selectedSparkSpec.defaults;
		}
		changed |= ImGui::SliderInt(
			"Workshop Spark Count", &workshopSparkCount, 1, 80);
		changed |= ImGui::SliderFloat(
			"Spark Lifetime", &selectedSpark.lifetime, 0.03f, 1.5f);
		changed |= ImGui::SliderFloat(
			"Spark Horizontal Speed", &selectedSpark.horizontalSpeed, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat(
			"Spark Vertical Min", &selectedSpark.verticalSpeedMin, -0.5f, 1.0f);
		changed |= ImGui::SliderFloat(
			"Spark Vertical Max", &selectedSpark.verticalSpeedMax, -0.5f, 1.0f);
		changed |= ImGui::SliderFloat(
			"Spark Scale Min", &selectedSpark.scaleMin, 0.02f, 2.0f);
		changed |= ImGui::SliderFloat(
			"Spark Scale Max", &selectedSpark.scaleMax, 0.02f, 2.0f);
		changed |= ImGui::SliderFloat(
			"Spark Gravity", &selectedSpark.gravity, -0.05f, 0.08f);
		changed |= ImGui::SliderFloat(
			"Spark Y Offset", &selectedSpark.yOffset, -2.0f, 4.0f);
		float sparkColor[4]{
			selectedSpark.colorR,
			selectedSpark.colorG,
			selectedSpark.colorB,
			selectedSpark.alpha,
		};
		if (ImGui::ColorEdit4("Spark Color", sparkColor)) {
			selectedSpark.colorR = sparkColor[0];
			selectedSpark.colorG = sparkColor[1];
			selectedSpark.colorB = sparkColor[2];
			selectedSpark.alpha = sparkColor[3];
			changed = true;
		}
		workshopSparkCopyTargetIndex = std::clamp(
			workshopSparkCopyTargetIndex,
			8,
			static_cast<int32_t>(kSparkBehaviorSpecs.size()) - 1);
		if (ImGui::BeginCombo(
			"Spark Copy Target",
			kSparkBehaviorSpecs[static_cast<size_t>(workshopSparkCopyTargetIndex)].key)) {
			for (int32_t index = 8;
				index < static_cast<int32_t>(kSparkBehaviorSpecs.size());
				++index) {
				const bool selected = index == workshopSparkCopyTargetIndex;
				if (ImGui::Selectable(
					kSparkBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					workshopSparkCopyTargetIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Copy Spark To Target")) {
			tuning_.sparkBehaviors[
				kSparkBehaviorSpecs[static_cast<size_t>(workshopSparkCopyTargetIndex)].key] =
				selectedSpark;
			selectedSparkIndex = workshopSparkCopyTargetIndex;
			changed = true;
		}
		ImGui::Separator();
		selectedSmokeIndex = std::clamp(
			selectedSmokeIndex,
			0,
			static_cast<int32_t>(kSmokeBehaviorSpecs.size()) - 1);
		const SmokeBehaviorSpec& selectedSmokeSpec =
			kSmokeBehaviorSpecs[static_cast<size_t>(selectedSmokeIndex)];
		if (ImGui::BeginCombo("Smoke Preset", selectedSmokeSpec.key)) {
			for (int32_t index = 0;
				index < static_cast<int32_t>(kSmokeBehaviorSpecs.size());
				++index) {
				const bool selected = index == selectedSmokeIndex;
				if (ImGui::Selectable(
					kSmokeBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					selectedSmokeIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		GameParticleEffects::SmokeBehaviorTuning& selectedSmoke =
			tuning_.smokeBehaviors[selectedSmokeSpec.key];
		if (selectedSmoke.lifetime <= 0.0f) {
			selectedSmoke = selectedSmokeSpec.defaults;
		}
		changed |= ImGui::SliderInt(
			"Workshop Smoke Count", &workshopSmokeCount, 1, 80);
		changed |= ImGui::SliderFloat(
			"Smoke Lifetime", &selectedSmoke.lifetime, 0.08f, 2.5f);
		changed |= ImGui::SliderFloat(
			"Smoke Offset Range", &selectedSmoke.offsetRange, 0.0f, 3.0f);
		changed |= ImGui::SliderFloat(
			"Smoke Horizontal Speed", &selectedSmoke.horizontalSpeed, 0.0f, 0.5f);
		changed |= ImGui::SliderFloat(
			"Smoke Vertical Min", &selectedSmoke.verticalSpeedMin, -0.2f, 0.5f);
		changed |= ImGui::SliderFloat(
			"Smoke Vertical Max", &selectedSmoke.verticalSpeedMax, -0.2f, 0.8f);
		changed |= ImGui::SliderFloat(
			"Smoke Scale Min", &selectedSmoke.scaleMin, 0.02f, 3.0f);
		changed |= ImGui::SliderFloat(
			"Smoke Scale Max", &selectedSmoke.scaleMax, 0.02f, 3.5f);
		changed |= ImGui::SliderFloat(
			"Smoke Scale Grow", &selectedSmoke.scaleGrow, -0.02f, 0.12f);
		changed |= ImGui::SliderFloat(
			"Smoke Y Offset", &selectedSmoke.yOffset, -1.0f, 3.0f);
		float smokeColor[4]{
			selectedSmoke.colorR,
			selectedSmoke.colorG,
			selectedSmoke.colorB,
			selectedSmoke.alpha,
		};
		if (ImGui::ColorEdit4("Smoke Color", smokeColor)) {
			selectedSmoke.colorR = smokeColor[0];
			selectedSmoke.colorG = smokeColor[1];
			selectedSmoke.colorB = smokeColor[2];
			selectedSmoke.alpha = smokeColor[3];
			changed = true;
		}
		workshopSmokeCopyTargetIndex = std::clamp(
			workshopSmokeCopyTargetIndex,
			3,
			static_cast<int32_t>(kSmokeBehaviorSpecs.size()) - 1);
		if (ImGui::BeginCombo(
			"Smoke Copy Target",
			kSmokeBehaviorSpecs[static_cast<size_t>(workshopSmokeCopyTargetIndex)].key)) {
			for (int32_t index = 3;
				index < static_cast<int32_t>(kSmokeBehaviorSpecs.size());
				++index) {
				const bool selected = index == workshopSmokeCopyTargetIndex;
				if (ImGui::Selectable(
					kSmokeBehaviorSpecs[static_cast<size_t>(index)].key,
					selected)) {
					workshopSmokeCopyTargetIndex = index;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Copy Smoke To Target")) {
			tuning_.smokeBehaviors[
				kSmokeBehaviorSpecs[static_cast<size_t>(workshopSmokeCopyTargetIndex)].key] =
				selectedSmoke;
			selectedSmokeIndex = workshopSmokeCopyTargetIndex;
			changed = true;
		}
	}
	if (ImGui::CollapsingHeader("Trail Bindings")) {
		for (const TrailBindingSpec& bindingSpec : kTrailBindingSpecs) {
			int32_t bindingIndex =
				FindTrailBehaviorIndex(GetTrailBinding(tuning_, bindingSpec));
			if (ImGui::BeginCombo(
				bindingSpec.usageKey,
				kTrailBehaviorSpecs[static_cast<size_t>(bindingIndex)].key)) {
				for (int32_t index = 0;
					index < static_cast<int32_t>(kTrailBehaviorSpecs.size());
					++index) {
					const bool selected = index == bindingIndex;
					if (ImGui::Selectable(
						kTrailBehaviorSpecs[static_cast<size_t>(index)].key,
						selected)) {
						bindingIndex = index;
						tuning_.trailBindings[bindingSpec.usageKey] =
							kTrailBehaviorSpecs[static_cast<size_t>(index)].key;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
	}
	if (ImGui::CollapsingHeader("Spark Bindings")) {
		for (const SparkBindingSpec& bindingSpec : kSparkBindingSpecs) {
			int32_t bindingIndex =
				FindSparkBehaviorIndex(GetSparkBinding(tuning_, bindingSpec));
			if (ImGui::BeginCombo(
				bindingSpec.usageKey,
				kSparkBehaviorSpecs[static_cast<size_t>(bindingIndex)].key)) {
				for (int32_t index = 0;
					index < static_cast<int32_t>(kSparkBehaviorSpecs.size());
					++index) {
					const bool selected = index == bindingIndex;
					if (ImGui::Selectable(
						kSparkBehaviorSpecs[static_cast<size_t>(index)].key,
						selected)) {
						bindingIndex = index;
						tuning_.sparkBindings[bindingSpec.usageKey] =
							kSparkBehaviorSpecs[static_cast<size_t>(index)].key;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
	}
	if (ImGui::CollapsingHeader("Smoke Bindings")) {
		for (const SmokeBindingSpec& bindingSpec : kSmokeBindingSpecs) {
			int32_t bindingIndex =
				FindSmokeBehaviorIndex(GetSmokeBinding(tuning_, bindingSpec));
			if (ImGui::BeginCombo(
				bindingSpec.usageKey,
				kSmokeBehaviorSpecs[static_cast<size_t>(bindingIndex)].key)) {
				for (int32_t index = 0;
					index < static_cast<int32_t>(kSmokeBehaviorSpecs.size());
					++index) {
					const bool selected = index == bindingIndex;
					if (ImGui::Selectable(
						kSmokeBehaviorSpecs[static_cast<size_t>(index)].key,
						selected)) {
						bindingIndex = index;
						tuning_.smokeBindings[bindingSpec.usageKey] =
							kSmokeBehaviorSpecs[static_cast<size_t>(index)].key;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
	}
	if (ImGui::CollapsingHeader("Emission Limits")) {
		for (const EmissionLimitSpec& spec : kEmissionLimitSpecs) {
			int32_t value = static_cast<int32_t>(GetEmissionLimit(tuning_, spec));
			if (ImGui::SliderInt(spec.key, &value, 0, 160)) {
				tuning_.emissionLimits[spec.key] =
					static_cast<uint32_t>((std::max)(0, value));
				changed = true;
			}
		}
	}
	if (changed) {
		ApplyTuning();
	}

	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	const TrailBehaviorSpec& workshopSpec =
		kTrailBehaviorSpecs[static_cast<size_t>(std::clamp(
			selectedTrailIndex,
			0,
			static_cast<int32_t>(kTrailBehaviorSpecs.size()) - 1))];
	const SparkBehaviorSpec& workshopSparkSpec =
		kSparkBehaviorSpecs[static_cast<size_t>(std::clamp(
			selectedSparkIndex,
			0,
			static_cast<int32_t>(kSparkBehaviorSpecs.size()) - 1))];
	const SmokeBehaviorSpec& workshopSmokeSpec =
		kSmokeBehaviorSpecs[static_cast<size_t>(std::clamp(
			selectedSmokeIndex,
			0,
			static_cast<int32_t>(kSmokeBehaviorSpecs.size()) - 1))];
	if (ImGui::Button("Preview Damage")) {
		particleManager->Emit(
			handles_.spark, previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDamageSparkCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Enemy Death")) {
		particleManager->Emit(
			GetSparkBindingHandle("enemyDeath"), previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.enemyDeathSparkCount)));
		particleManager->Emit(
			GetSmokeBindingHandle("enemyDeathSmoke"), previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.enemyDeathSmokeCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Player Death")) {
		particleManager->Emit(
			GetSparkBindingHandle("playerDeath"), previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.playerDeathSparkCount)));
		particleManager->Emit(
			GetSmokeBindingHandle("playerDeathSmoke"), previewPosition,
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
			GetSmokeBindingHandle("explosionSmoke"), previewPosition,
			static_cast<uint32_t>((std::max)(0, tuning_.explosionSmokeCount)));
		particleManager->Emit(handles_.ripple, previewPosition, 4u);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Bow Trail")) {
		const Vector3 end{
			previewPosition.x,
			previewPosition.y + 0.8f,
			previewPosition.z + 4.0f * tuning_.bowTrailLengthMultiplier,
		};
		particleManager->EmitTrailSegment(
			handles_.bowArrowTrail,
			{ previewPosition.x, previewPosition.y + 0.8f, previewPosition.z },
			end,
			tuning_.bowTrailWidth);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Workshop Line")) {
		particleManager->EmitTrailSegment(
			handles_.*(workshopSpec.handle),
			{ previewPosition.x, previewPosition.y + 0.8f, previewPosition.z },
			{
				previewPosition.x,
				previewPosition.y + 0.8f,
				previewPosition.z + workshopLength,
			},
			workshopWidth);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Workshop Circle")) {
		particleManager->EmitTrailCircle(
			handles_.*(workshopSpec.handle),
			previewPosition,
			workshopRadius,
			0.35f,
			workshopWidth,
			workshopSegments);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Workshop Burst")) {
		const int32_t segments = (std::max)(3, workshopSegments);
		for (int32_t index = 0; index < segments; ++index) {
			const float angle =
				static_cast<float>(index) /
				static_cast<float>(segments) * 6.28318530718f;
			const Vector3 end{
				previewPosition.x + std::cos(angle) * workshopRadius,
				previewPosition.y + 0.8f,
				previewPosition.z + std::sin(angle) * workshopRadius,
			};
			particleManager->EmitTrailSegment(
				handles_.*(workshopSpec.handle),
				{ previewPosition.x, previewPosition.y + 0.8f, previewPosition.z },
				end,
				workshopWidth);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Workshop Spark")) {
		particleManager->Emit(
			handles_.*(workshopSparkSpec.handle),
			previewPosition,
			static_cast<uint32_t>((std::max)(1, workshopSparkCount)));
	}
	ImGui::SameLine();
	if (ImGui::Button("Preview Workshop Smoke")) {
		particleManager->Emit(
			handles_.*(workshopSmokeSpec.handle),
			previewPosition,
			static_cast<uint32_t>((std::max)(1, workshopSmokeCount)));
	}
	if (ImGui::Button("Save Particle Tuning")) {
		std::vector<UILayoutIO::Entry> entries;
		AppendTuningEntries(entries);
		UILayoutIO::Save(DataPaths::kDebugTuning, entries);
	}
}
#endif

}
