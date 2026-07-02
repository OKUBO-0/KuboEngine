#pragma once

#include "Vector3.h"

namespace DirectXGame {

class EnemyManager;
class GameParticleEffects;

class BossPresentation final {
public:
	void Reset();
	void StartEntrance(EnemyManager& enemyManager);
	bool UpdateEntrance(
		EnemyManager& enemyManager,
		const GameParticleEffects& particleEffects,
		float deltaTime);

	void StartDefeat(EnemyManager& enemyManager);
	bool UpdateDefeat(
		EnemyManager& enemyManager,
		const GameParticleEffects& particleEffects,
		float deltaTime);

private:
	void CaptureCamera(Vector3& position, Vector3& rotation);
	void UpdateEntranceCamera(float progress);
	void UpdateDefeatCamera(float progress);

	float entranceTimer_ = 0.0f;
	float defeatTimer_ = 0.0f;
	Vector3 entranceFocusPosition_{};
	Vector3 entranceStartBossPosition_{};
	Vector3 defeatFocusPosition_{};
	Vector3 entranceStartCameraPosition_{};
	Vector3 entranceStartCameraRotation_{};
	Vector3 defeatStartCameraPosition_{};
	Vector3 defeatStartCameraRotation_{};
	bool entranceEffectEmitted_ = false;
	bool defeatEffectEmitted_ = false;
	bool resultTransitionRequested_ = false;
};

}
