#include "game/directxgame/core/PlayerGizmoDebugPanel.h"
#include "CameraManager.h"
#include "ImGuizmoManager.h"
#include "MyMath.h"
#include "game/directxgame/player/Player.h"
#include <algorithm>
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void PlayerGizmoDebugPanel::Draw(bool* open, Player* player)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(920.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 240.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("ギズモ", open);
	ImGui::TextUnformatted("Target: Player");
	constexpr const char* kOperationLabels[] = {
		"Translate",
		"Rotate",
		"Scale",
	};
	if (ImGui::Combo(
			"Operation",
			&operation_,
			kOperationLabels,
			static_cast<int32_t>(std::size(kOperationLabels)))) {
		operation_ = (std::clamp)(operation_, 0, 2);
	}
	ImGui::Text(
		"Using: %s",
		Engine::Editor::ImGuizmoManager::IsUsing() ? "true" : "false");

	Engine::CameraSystem::Camera* activeCamera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		ImGui::TextUnformatted("Active camera is not available.");
		ImGui::End();
		return;
	}
	if (!player) {
		ImGui::TextUnformatted("Selected target is not available.");
		ImGui::End();
		return;
	}

	const Vector3 targetPosition = player->GetWorldPosition();
	const float targetRotationY = player->GetWorldRotationY();
	float translation[3]{
		targetPosition.x,
		targetPosition.y,
		targetPosition.z,
	};
	float rotation[3]{
		0.0f,
		targetRotationY * 180.0f / 3.14159265f,
		0.0f,
	};
	float scale[3]{ 1.0f, 1.0f, 1.0f };
	Matrix4x4 objectMatrix = MyMath::MakeAffineMatrix(
		Vector3{ scale[0], scale[1], scale[2] },
		Vector3{ 0.0f, targetRotationY, 0.0f },
		targetPosition);
	ImGui::DragFloat3("Position", translation, 0.25f, -160.0f, 160.0f);
	ImGui::DragFloat("Rotation Y", &rotation[1], 1.0f, -180.0f, 180.0f);
	Engine::Editor::ImGuizmoManager::RecomposeMatrixFromComponents(
		translation,
		rotation,
		scale,
		&objectMatrix.m[0][0]);

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	Engine::Editor::ImGuizmoManager::SetRect(
		viewport->Pos.x,
		viewport->Pos.y,
		viewport->Size.x,
		viewport->Size.y);
	const auto operation =
		static_cast<Engine::Editor::ImGuizmoManager::Operation>(operation_);
	if (Engine::Editor::ImGuizmoManager::Manipulate(
			&activeCamera->GetViewMatrix().m[0][0],
			&activeCamera->GetProjectionMatrix().m[0][0],
			&objectMatrix.m[0][0],
			operation,
			Engine::Editor::ImGuizmoManager::Mode::World)) {
		Engine::Editor::ImGuizmoManager::DecomposeMatrixToComponents(
			&objectMatrix.m[0][0],
			translation,
			rotation,
			scale);
	}
	player->SetDebugWorldPosition({
		translation[0],
		translation[1],
		translation[2],
	});
	player->SetDebugWorldRotationY(rotation[1] * 3.14159265f / 180.0f);
	ImGui::End();
#else
	(void)open;
	(void)player;
#endif
}

}
