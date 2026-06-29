#include "RenderingDebugPanel.h"
#include "CameraManager.h"
#include "OffscreenRenderManager.h"
#include "SceneLighting.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void DebugUI::Rendering::DrawLighting(
	bool* open,
	bool& debugCameraEnabled,
	Vector3& debugCameraPosition,
	Vector3& debugCameraRotation,
	bool& lightDebugDrawEnabled,
	bool& debugDrawEnabled,
	const std::function<void()>& updateDebugCamera)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(730.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("ライト設定", open);
	bool useDebugCamera = debugCameraEnabled;
	if (ImGui::Checkbox("Use Debug Camera", &useDebugCamera)) {
		debugCameraEnabled = useDebugCamera;
		if (updateDebugCamera) {
			updateDebugCamera();
		}
		if (!debugCameraEnabled) {
			Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(
				"directxgame_player");
		}
	}

	float position[3]{
		debugCameraPosition.x,
		debugCameraPosition.y,
		debugCameraPosition.z,
	};
	if (ImGui::DragFloat3(
			"Debug Camera Position",
			position,
			0.5f,
			-220.0f,
			220.0f)) {
		debugCameraPosition = { position[0], position[1], position[2] };
		if (updateDebugCamera) {
			updateDebugCamera();
		}
	}

	float rotation[3]{
		debugCameraRotation.x,
		debugCameraRotation.y,
		debugCameraRotation.z,
	};
	if (ImGui::DragFloat3(
			"Debug Camera Rotation",
			rotation,
			0.01f,
			-3.14f,
			3.14f)) {
		debugCameraRotation = { rotation[0], rotation[1], rotation[2] };
		if (updateDebugCamera) {
			updateDebugCamera();
		}
	}

	bool drawLightMarkers = lightDebugDrawEnabled;
	if (ImGui::Checkbox("Draw Light Markers", &drawLightMarkers)) {
		lightDebugDrawEnabled = drawLightMarkers;
		debugDrawEnabled = debugDrawEnabled || lightDebugDrawEnabled;
	}
	SceneLighting::DrawDebugUI();
	ImGui::End();
#else
	(void)open;
	(void)debugCameraEnabled;
	(void)debugCameraPosition;
	(void)debugCameraRotation;
	(void)lightDebugDrawEnabled;
	(void)debugDrawEnabled;
	(void)updateDebugCamera;
#endif
}

void DebugUI::Rendering::DrawOffscreen(bool* open)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}
	if (Engine::Base::OffscreenRenderManager* offscreen =
		Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->DrawImGui(open);
	}
#else
	(void)open;
#endif
}

}
