#include "DebugContext.h"
#include "CameraManager.h"
#include "DataPaths.h"
#include "GameAudioTuning.h"
#include "SceneLighting.h"
#include "UILayoutIO.h"
#include "EnemyView.h"
#include "GameParticleEffects.h"
#include "Player.h"
#include "ProductionTuning.h"
#include <cstdint>
#include <string>
#include <vector>

namespace DirectXGame {

void DebugContext::InitializeCamera()
{
	camera_.SetTranslate(cameraPosition_);
	camera_.SetRotate(cameraRotation_);
	camera_.SetFarClip(500.0f);
	camera_.Update();
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(
		"directxgame_debug",
		&camera_);
}

void DebugContext::FinalizeCamera()
{
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera(
		"directxgame_debug");
}

void DebugContext::UpdateCamera()
{
	camera_.SetTranslate(cameraPosition_);
	camera_.SetRotate(cameraRotation_);
	camera_.SetFarClip(500.0f);
	camera_.Update();
	Engine::CameraSystem::CameraManager* cameraManager =
		Engine::CameraSystem::CameraManager::GetInstance();
	if (cameraEnabled_) {
		cameraManager->SetActiveCamera("directxgame_debug");
	}
}

void DebugContext::Load(
	Player* player,
	GameParticleEffects& particleEffects)
{
	const UILayoutIO::LayoutMap tuning =
		UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {});
	auto loadWindowVisible =
		[&tuning](const char* key, bool fallback) {
			return UILayoutIO::GetFloat(
				tuning,
				std::string("debugWindow.") + key,
				fallback ? 1.0f : 0.0f) > 0.5f;
		};

	windows_.windowSwitcher =
		loadWindowVisible("windowSwitcher", windows_.windowSwitcher);
	windows_.sceneView =
		loadWindowVisible("sceneView", windows_.sceneView);
	windows_.objectView =
		loadWindowVisible("objectView", windows_.objectView);
	windows_.particleView =
		loadWindowVisible("particleView", windows_.particleView);
	windows_.statisticsView =
		loadWindowVisible("statisticsView", windows_.statisticsView);
	windows_.offscreenSettings =
		loadWindowVisible("offscreenSettings", windows_.offscreenSettings);
	windows_.lightSettings =
		loadWindowVisible("lightSettings", windows_.lightSettings);
	windows_.gizmo =
		loadWindowVisible("gizmo", windows_.gizmo);
	windows_.objectManager =
		loadWindowVisible("objectManager", windows_.objectManager);
	windows_.motionEditor =
		loadWindowVisible("motionEditor", windows_.motionEditor);
	windows_.spriteManager =
		loadWindowVisible("spriteManager", windows_.spriteManager);
	windows_.colliderTagManager =
		loadWindowVisible("colliderTagManager", windows_.colliderTagManager);
	windows_.audio =
		loadWindowVisible("audio", windows_.audio);
	windows_.keyInputDebug =
		loadWindowVisible("keyInputDebug", windows_.keyInputDebug);
	windows_.hotReload =
		loadWindowVisible("hotReload", windows_.hotReload);
	windows_.sceneSettings =
		loadWindowVisible("sceneSettings", windows_.sceneSettings);
	windows_.sceneSpecificDebug =
		loadWindowVisible(
			"sceneSpecificDebug",
			windows_.sceneSpecificDebug);
	windows_.objectSettings =
		loadWindowVisible("objectSettings", windows_.objectSettings);

	LoadGameAudioTuning(tuning);
	particleEffects.LoadTuning(tuning);
	EnemyView::LoadVisualTuning(tuning);

	(void)player;

	cameraPosition_ = UILayoutIO::GetVector3(
		tuning,
		"debugCamera.position",
		cameraPosition_);
	cameraRotation_ = UILayoutIO::GetVector3(
		tuning,
		"debugCamera.rotation",
		cameraRotation_);
	cameraEnabled_ =
		UILayoutIO::GetFloat(
			tuning,
			"debugCamera.enabled",
			cameraEnabled_ ? 1.0f : 0.0f) > 0.5f;
	lightDrawEnabled_ =
		UILayoutIO::GetFloat(
			tuning,
			"light.debugDrawEnabled",
			lightDrawEnabled_ ? 1.0f : 0.0f) > 0.5f;
	SceneLighting::Load(tuning);

	const ProductionTuning::NumberMap productionTuning =
		ProductionTuning::LoadOrCreate(DataPaths::kProductionTuning);
	ProductionTuning::ApplyToPlayer(productionTuning, player);
	ProductionTuning::ApplyToParticles(productionTuning, particleEffects);
}

void DebugContext::Save(
	const Player* player,
	const GameParticleEffects& particleEffects) const
{
#ifdef _DEBUG
	std::vector<UILayoutIO::Entry> entries;
	AppendGameAudioTuningEntries(entries);
	particleEffects.AppendTuningEntries(entries);
	EnemyView::AppendVisualTuningEntries(entries);
	auto saveWindowVisible =
		[&entries](const char* key, bool visible) {
			entries.push_back({
				std::string("debugWindow.") + key,
				{ visible ? 1.0f : 0.0f },
			});
		};
	saveWindowVisible("windowSwitcher", windows_.windowSwitcher);
	saveWindowVisible("sceneView", windows_.sceneView);
	saveWindowVisible("objectView", windows_.objectView);
	saveWindowVisible("particleView", windows_.particleView);
	saveWindowVisible("statisticsView", windows_.statisticsView);
	saveWindowVisible("offscreenSettings", windows_.offscreenSettings);
	saveWindowVisible("lightSettings", windows_.lightSettings);
	saveWindowVisible("gizmo", windows_.gizmo);
	saveWindowVisible("objectManager", windows_.objectManager);
	saveWindowVisible("motionEditor", windows_.motionEditor);
	saveWindowVisible("spriteManager", windows_.spriteManager);
	saveWindowVisible("colliderTagManager", windows_.colliderTagManager);
	saveWindowVisible("audio", windows_.audio);
	saveWindowVisible("keyInputDebug", windows_.keyInputDebug);
	saveWindowVisible("hotReload", windows_.hotReload);
	saveWindowVisible("sceneSettings", windows_.sceneSettings);
	saveWindowVisible("sceneSpecificDebug", windows_.sceneSpecificDebug);
	saveWindowVisible("objectSettings", windows_.objectSettings);
	if (player) {
		entries.push_back({
			"camera.height",
			{ player->GetCameraHeight() },
		});
		entries.push_back({
			"camera.distance",
			{ player->GetCameraDistance() },
		});
		entries.push_back({
			"camera.pitch",
			{ player->GetCameraPitch() },
		});
		entries.push_back({
			"camera.followSmoothness",
			{ player->GetCameraFollowSmoothness() },
		});
		entries.push_back({
			"camera.mode",
			{ static_cast<float>(player->GetCameraMode()) },
		});
		entries.push_back({
			"camera.mouseAimEnabled",
			{ player->IsMouseAimEnabled() ? 1.0f : 0.0f },
		});
	}
	entries.push_back({
		"debugCamera.enabled",
		{ cameraEnabled_ ? 1.0f : 0.0f },
	});
	entries.push_back({
		"debugCamera.position",
		{ cameraPosition_.x, cameraPosition_.y, cameraPosition_.z },
	});
	entries.push_back({
		"debugCamera.rotation",
		{ cameraRotation_.x, cameraRotation_.y, cameraRotation_.z },
	});
	entries.push_back({
		"light.debugDrawEnabled",
		{ lightDrawEnabled_ ? 1.0f : 0.0f },
	});
	SceneLighting::AppendTuningEntries(entries);
	UILayoutIO::Save(DataPaths::kDebugTuning, entries);

	ProductionTuning::NumberMap productionTuning =
		ProductionTuning::LoadOrCreate(DataPaths::kProductionTuning);
	ProductionTuning::CaptureFromPlayer(productionTuning, player);
	ProductionTuning::CaptureFromParticles(productionTuning, particleEffects);
	ProductionTuning::Save(DataPaths::kProductionTuning, productionTuning);
#else
	(void)player;
	(void)particleEffects;
#endif
}

}
