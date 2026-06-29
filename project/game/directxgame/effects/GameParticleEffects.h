#pragma once

#include "ParticleManager.h"
#include "Vector3.h"
#include "UILayoutIO.h"
#include <cstdint>
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
		Engine::Particle::ParticleGroupHandle playerDeathSpark{};
		Engine::Particle::ParticleGroupHandle deathSmoke{};
		Engine::Particle::ParticleGroupHandle confetti{};
		Engine::Particle::ParticleGroupHandle suicideEnemyTrail{};
	};

	struct Tuning {
		int32_t playerDamageSparkCount = 18;
		int32_t playerDamageRippleCount = 1;
		int32_t enemyHitSparkCount = 10;
		int32_t enemyDeathSparkCount = 24;
		int32_t enemyDeathSmokeCount = 10;
		int32_t expSparkCount = 8;
		int32_t lightningSparkCount = 14;
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
	};

	void Initialize();
	void LoadTuning(const UILayoutIO::LayoutMap& tuning);
	void AppendTuningEntries(std::vector<UILayoutIO::Entry>& entries) const;
	void ApplyTuning() const;

	const Tuning& GetTuning() const { return tuning_; }
	const Handles& GetHandles() const { return handles_; }

#ifdef _DEBUG
	void DrawDebugUI(const Vector3& previewPosition);
#endif

private:
	Handles handles_{};
	Tuning tuning_{};
	bool initialized_ = false;
};

}
