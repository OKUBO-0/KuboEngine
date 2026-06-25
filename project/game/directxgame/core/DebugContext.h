#pragma once

#include "Camera.h"
#include "game/directxgame/core/DebugEditorShell.h"
#include "game/directxgame/core/StatisticsDebugPanel.h"
#include "game/directxgame/core/PlayerGizmoDebugPanel.h"

namespace DirectXGame {

class GameParticleEffects;
class Player;

class DebugContext final {
public:
	void InitializeCamera();
	void FinalizeCamera();
	void UpdateCamera();
	void Load(Player* player, GameParticleEffects& particleEffects);
	void Save(
		const Player* player,
		const GameParticleEffects& particleEffects) const;

	DebugWindowVisibility& Windows() { return windows_; }
	const DebugWindowVisibility& Windows() const { return windows_; }
	StatisticsDebugPanel& StatisticsPanel() {
		return statisticsPanel_;
	}
	PlayerGizmoDebugPanel& GizmoPanel() { return gizmoPanel_; }

	bool IsGameplayFrozen() const { return freezeGameplay_; }
	bool& GameplayFrozen() { return freezeGameplay_; }
	bool IsCollisionDrawEnabled() const { return collisionDrawEnabled_; }
	bool& CollisionDrawEnabled() { return collisionDrawEnabled_; }
	bool IsLightDrawEnabled() const { return lightDrawEnabled_; }
	bool& LightDrawEnabled() { return lightDrawEnabled_; }
	bool& CameraEnabled() { return cameraEnabled_; }
	Vector3& CameraPosition() { return cameraPosition_; }
	Vector3& CameraRotation() { return cameraRotation_; }

private:
	Engine::CameraSystem::Camera camera_{};
	Vector3 cameraPosition_{ 0.0f, 85.0f, -85.0f };
	Vector3 cameraRotation_{ 0.82f, 0.0f, 0.0f };
	DebugWindowVisibility windows_{};
	StatisticsDebugPanel statisticsPanel_{};
	PlayerGizmoDebugPanel gizmoPanel_{};
	bool cameraEnabled_ = false;
	bool lightDrawEnabled_ = false;
	bool collisionDrawEnabled_ = false;
	bool freezeGameplay_ = false;
};

}
