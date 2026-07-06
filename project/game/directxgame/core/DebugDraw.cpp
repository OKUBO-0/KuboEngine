#include "DebugDraw.h"
#include "Line.h"
#include "Object3DCommon.h"
#include "Enemy.h"
#include "EnemyManager.h"
#include "ExpOrb.h"
#include "Player.h"
#include "PlayerManager.h"
#include "NormalBullet.h"
#include "OrbitBullet.h"
#include <cmath>

namespace {

bool BuildClampedDebugSegment(
	const Vector3& start,
	const Vector3& end,
	float maxLength,
	Vector3& clampedEnd)
{
	const Vector3 delta{
		end.x - start.x,
		end.y - start.y,
		end.z - start.z,
	};
	const float length = std::sqrt(
		delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
	if (length <= 0.001f) {
		return false;
	}
	if (length <= maxLength) {
		clampedEnd = end;
		return true;
	}
	const float scale = maxLength / length;
	clampedEnd = {
		start.x + delta.x * scale,
		start.y + delta.y * scale,
		start.z + delta.z * scale,
	};
	return true;
}

}

namespace DirectXGame {

void DebugDraw::Queue(
	bool collisionEnabled,
	bool lightEnabled,
	const Player* player,
	const EnemyManager* enemyManager,
	const PlayerManager* playerManager)
{
#ifdef _DEBUG
	if (!collisionEnabled || !player) {
		return;
	}

	Engine::LineSystem::Line line;
	const Vector3 playerPosition = player->GetWorldPosition();
	line.DrawOBB(
		player->GetCollisionObb(),
		{ 0.25f, 0.95f, 1.0f, 1.0f });
	line.DrawSphere(
		playerPosition,
		50.0f,
		{ 0.25f, 0.55f, 1.0f, 0.45f });

	if (enemyManager) {
		for (const std::unique_ptr<Enemy>& enemy :
			enemyManager->GetEnemies()) {
			if (enemy && enemy->IsActive()) {
				line.DrawOBB(
					enemy->GetCollisionObb(),
					{ 1.0f, 0.2f, 0.18f, 1.0f });
			}
		}
		for (const std::unique_ptr<ExpOrb>& orb :
			enemyManager->GetExpOrbs()) {
			if (!orb || !orb->IsActive()) {
				continue;
			}
			const Vector3 orbPosition = orb->GetPosition();
			line.DrawSphere(
				orbPosition,
				0.65f,
				{ 0.3f, 1.0f, 0.58f, 0.8f });
			const float dx = orbPosition.x - playerPosition.x;
			const float dz = orbPosition.z - playerPosition.z;
			if (dx * dx + dz * dz <= 144.0f) {
				line.Draw(
					{
						playerPosition.x,
						playerPosition.y + 1.0f,
						playerPosition.z,
					},
					{
						orbPosition.x,
						orbPosition.y + 0.5f,
						orbPosition.z,
					},
					{ 0.3f, 1.0f, 0.58f, 0.65f });
			}
		}
	}

	if (playerManager) {
		for (const std::unique_ptr<NormalBullet>& bullet :
			playerManager->GetNormalBullets()) {
			if (bullet && bullet->IsActive()) {
				line.DrawOBB(
					bullet->GetCollisionObb(),
					{ 1.0f, 0.65f, 0.15f, 1.0f });
				Vector3 clampedEnd{};
				if (BuildClampedDebugSegment(
						bullet->GetPreviousPosition(),
						bullet->GetPosition(),
						4.0f,
						clampedEnd)) {
					line.Draw(
						bullet->GetPreviousPosition(),
						clampedEnd,
						{ 1.0f, 0.65f, 0.15f, 0.75f });
				}
			}
		}
		for (const std::unique_ptr<OrbitBullet>& bullet :
			playerManager->GetOrbitBullets()) {
			if (bullet && bullet->IsActive()) {
				line.DrawOBB(
					bullet->GetCollisionObb(),
					{ 0.7f, 0.35f, 1.0f, 1.0f });
			}
		}
		if (playerManager->GetLightningEffectTimer() > 0.0f) {
			for (const Vector3& target :
				playerManager->GetLightningEffectTargets()) {
				line.DrawSphere(
					target,
					playerManager->GetLightningRadius(),
					{ 0.4f, 0.75f, 1.0f, 0.55f });
			}
		}
	}

	if (lightEnabled) {
		const SceneLightData& sceneLight =
			Engine::Graphics3D::Object3DCommon::GetInstance()->GetSceneLight();
		if (sceneLight.enable) {
			const Vector3 start =
				playerPosition + Vector3{ 0.0f, 22.0f, 0.0f };
			const Vector3 end{
				start.x + sceneLight.direction.x * 16.0f,
				start.y + sceneLight.direction.y * 16.0f,
				start.z + sceneLight.direction.z * 16.0f,
			};
			line.Draw(
				start,
				end,
				{ 1.0f, 1.0f, 0.55f, 1.0f });
			line.DrawSphere(
				start,
				0.9f,
				{ 1.0f, 1.0f, 0.55f, 1.0f });
		}
	}
#else
	(void)collisionEnabled;
	(void)lightEnabled;
	(void)player;
	(void)enemyManager;
	(void)playerManager;
#endif
}

}
