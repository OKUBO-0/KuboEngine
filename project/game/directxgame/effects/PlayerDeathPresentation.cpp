#include "game/directxgame/effects/PlayerDeathPresentation.h"
#include "ParticleManager.h"
#include "game/directxgame/effects/GameParticleEffects.h"
#include "game/directxgame/player/Player.h"
#include <algorithm>
#include <cstdint>

namespace {

constexpr float kPresentationDuration = 1.35f;
constexpr float kTransitionStartTime = 1.55f;
constexpr float kOverlayDelay = 1.25f;
constexpr float kOverlayFadeDuration = 0.45f;
constexpr float kOverlayMaxAlpha = 0.65f;

float Clamp01(float value)
{
	return (std::clamp)(value, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void PlayerDeathPresentation::Reset()
{
	elapsedTime_ = 0.0f;
	transitionRequested_ = false;
}

void PlayerDeathPresentation::Start(
	Player& player,
	const GameParticleEffects& particleEffects)
{
	Reset();
	player.StartDeathPresentation();

	const GameParticleEffects::Tuning& tuning = particleEffects.GetTuning();
	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	const Vector3& position = player.GetWorldPosition();
	particleManager->Emit(
		"DirectXGame.PlayerDeathSpark",
		position,
		static_cast<uint32_t>((std::max)(0, tuning.playerDeathSparkCount)));
	particleManager->Emit(
		"DirectXGame.DeathSmoke",
		position,
		static_cast<uint32_t>((std::max)(0, tuning.playerDeathSmokeCount)));
	particleManager->Emit(
		"DirectXGame.Ripple",
		position,
		static_cast<uint32_t>((std::max)(0, tuning.playerDeathRippleCount)));
}

bool PlayerDeathPresentation::Update(
	Player& player,
	float deltaTime,
	bool skipRequested)
{
	elapsedTime_ += deltaTime;
	player.UpdateDeathPresentation(elapsedTime_, kPresentationDuration);
	if (!transitionRequested_ &&
		(elapsedTime_ >= kTransitionStartTime || skipRequested)) {
		transitionRequested_ = true;
		return true;
	}
	return false;
}

float PlayerDeathPresentation::GetOverlayAlpha() const
{
	return kOverlayMaxAlpha *
		Clamp01((elapsedTime_ - kOverlayDelay) / kOverlayFadeDuration);
}

}
