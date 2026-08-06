#pragma once

#include "ParticleManager.h"
#include "Vector3.h"
#include "UILayoutIO.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class GameParticleEffects final {
public:
	struct Handles {
		Engine::Particle::ParticleGroupHandle ripple{};
		Engine::Particle::ParticleGroupHandle spark{};
		Engine::Particle::ParticleGroupHandle enemyHitSpark{};
		Engine::Particle::ParticleGroupHandle expSpark{};
		Engine::Particle::ParticleGroupHandle lightningImpact{};
		Engine::Particle::ParticleGroupHandle explosionBurst{};
		Engine::Particle::ParticleGroupHandle explosionSmoke{};
		Engine::Particle::ParticleGroupHandle playerDeathSpark{};
		Engine::Particle::ParticleGroupHandle deathSmoke{};
		Engine::Particle::ParticleGroupHandle confetti{};
		Engine::Particle::ParticleGroupHandle suicideEnemyTrail{};
		Engine::Particle::ParticleGroupHandle bowArrowTrail{};
		Engine::Particle::ParticleGroupHandle flameProjectileTrail{};
		Engine::Particle::ParticleGroupHandle flameProjectileGlow{};
		Engine::Particle::ParticleGroupHandle handgunBulletTrail{};
		Engine::Particle::ParticleGroupHandle boomerangTrail{};
		Engine::Particle::ParticleGroupHandle rockTrail{};
		Engine::Particle::ParticleGroupHandle boneTrail{};
		Engine::Particle::ParticleGroupHandle lightningTrail{};
		Engine::Particle::ParticleGroupHandle swordSlashTrail{};
		Engine::Particle::ParticleGroupHandle auraPulseTrail{};
		Engine::Particle::ParticleGroupHandle auraGlow{};
		Engine::Particle::ParticleGroupHandle flameShoeTrail{};
		Engine::Particle::ParticleGroupHandle flameShoeGlow{};
		Engine::Particle::ParticleGroupHandle handgunMuzzleFlash{};
		Engine::Particle::ParticleGroupHandle handgunReloadSmoke{};
		Engine::Particle::ParticleGroupHandle scratchTrail{};
		Engine::Particle::ParticleGroupHandle customTrail0{};
		Engine::Particle::ParticleGroupHandle customTrail1{};
		Engine::Particle::ParticleGroupHandle customTrail2{};
		Engine::Particle::ParticleGroupHandle customTrail3{};
		Engine::Particle::ParticleGroupHandle customSpark0{};
		Engine::Particle::ParticleGroupHandle customSpark1{};
		Engine::Particle::ParticleGroupHandle customSmoke0{};
		Engine::Particle::ParticleGroupHandle customSmoke1{};
	};

	struct TrailBehaviorTuning {
		float lifetime = 0.16f;
		float shrinkRate = 0.82f;
		float fadeInRatio = 0.12f;
		float fadeOutPower = 1.8f;
		float colorR = 1.0f;
		float colorG = 1.0f;
		float colorB = 1.0f;
		float alpha = 1.0f;
	};

	struct SparkBehaviorTuning {
		float lifetime = 0.35f;
		float horizontalSpeed = 0.18f;
		float verticalSpeedMin = 0.06f;
		float verticalSpeedMax = 0.22f;
		float scaleMin = 0.18f;
		float scaleMax = 0.34f;
		float gravity = 0.012f;
		float yOffset = 0.8f;
		float colorR = 1.0f;
		float colorG = 0.35f;
		float colorB = 0.25f;
		float alpha = 1.0f;
	};

	struct SmokeBehaviorTuning {
		float lifetime = 0.72f;
		float offsetRange = 0.65f;
		float horizontalSpeed = 0.035f;
		float verticalSpeedMin = 0.025f;
		float verticalSpeedMax = 0.075f;
		float scaleMin = 0.45f;
		float scaleMax = 0.85f;
		float scaleGrow = 0.018f;
		float yOffset = 0.75f;
		float colorR = 0.45f;
		float colorG = 0.42f;
		float colorB = 0.38f;
		float alpha = 0.85f;
	};

	struct Tuning {
		int32_t playerDamageSparkCount = 18;
		int32_t playerDamageRippleCount = 1;
		int32_t enemyHitSparkCount = 10;
		int32_t enemyDeathSparkCount = 24;
		int32_t enemyDeathSmokeCount = 10;
		int32_t expSparkCount = 8;
		int32_t lightningSparkCount = 14;
		int32_t explosionBurstCount = 44;
		int32_t explosionSmokeCount = 18;
		int32_t levelUpConfettiCount = 120;
		int32_t playerDeathSparkCount = 48;
		int32_t playerDeathSmokeCount = 18;
		int32_t playerDeathRippleCount = 2;
		float sparkLifetime = 0.35f;
		float sparkVelocityScale = 1.0f;
		float sparkScaleMultiplier = 1.0f;
		float smokeLifetime = 0.72f;
		float smokeScaleMultiplier = 1.0f;
		float rippleLifetime = 0.42f;
		float rippleExpandSpeed = 4.5f;
		float confettiVelocityScale = 0.55f;
		float confettiScaleMultiplier = 0.55f;
		float particleEmissionScale = 1.0f;
		float bowTrailWidth = 0.18f;
		float bowTrailLengthMultiplier = 1.20f;
		float bowTrailMaxFrameDistance = 3.4f;
		float bowTrailLifetime = 0.18f;
		float bowTrailShrinkRate = 0.88f;
		float bowTrailFadeInRatio = 0.03f;
		float bowTrailFadeOutPower = 2.1f;
		float bowTrailColorR = 0.58f;
		float bowTrailColorG = 0.88f;
		float bowTrailColorB = 1.0f;
		float bowTrailAlpha = 0.62f;
		std::unordered_map<std::string, uint32_t> emissionLimits;
		std::unordered_map<std::string, TrailBehaviorTuning> trailBehaviors;
		std::unordered_map<std::string, SparkBehaviorTuning> sparkBehaviors;
		std::unordered_map<std::string, SmokeBehaviorTuning> smokeBehaviors;
		std::unordered_map<std::string, std::string> trailBindings;
		std::unordered_map<std::string, std::string> sparkBindings;
		std::unordered_map<std::string, std::string> smokeBindings;
	};

	void Initialize();
	void LoadTuning(const UILayoutIO::LayoutMap& tuning);
	void AppendTuningEntries(std::vector<UILayoutIO::Entry>& entries) const;
	void ApplyTuning() const;

	const Tuning& GetTuning() const { return tuning_; }
	const Handles& GetHandles() const { return handles_; }
	Engine::Particle::ParticleGroupHandle GetTrailBindingHandle(
		const std::string& usageKey) const;
	Engine::Particle::ParticleGroupHandle GetSparkBindingHandle(
		const std::string& usageKey) const;
	Engine::Particle::ParticleGroupHandle GetSmokeBindingHandle(
		const std::string& usageKey) const;

#ifdef _DEBUG
	void DrawDebugUI(const Vector3& previewPosition);
#endif

private:
	Handles handles_{};
	Tuning tuning_{};
	bool initialized_ = false;
};

}
