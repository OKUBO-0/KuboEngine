#include "DebugInput.h"
#include "Model.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

namespace {

Vector2 GetKeyboardMove(Engine::InputSystem::Input* input)
{
	Vector2 move{ 0.0f, 0.0f };
	if (!input) {
		return move;
	}
	if (input->PushKey(DIK_W) || input->PushKey(DIK_UP)) { move.y += 1.0f; }
	if (input->PushKey(DIK_S) || input->PushKey(DIK_DOWN)) { move.y -= 1.0f; }
	if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT)) { move.x -= 1.0f; }
	if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { move.x += 1.0f; }
	return move;
}

Vector2 GetGamepadMove(Engine::InputSystem::Input* input)
{
	if (!input) {
		return { 0.0f, 0.0f };
	}
	Vector2 move{
		GameInputBindings::ClampAxis(input->GetGamePadStickX()),
		GameInputBindings::ClampAxis(input->GetGamePadStickY()),
	};
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_UP)) { move.y += 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_DOWN)) { move.y -= 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_LEFT)) { move.x -= 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_RIGHT)) { move.x += 1.0f; }
	return move;
}

const char* BoolText(bool value)
{
	return value ? "true" : "false";
}

}

void DebugUI::Input::Draw(
	bool* open,
	Engine::InputSystem::Input* input,
	const DebugInputState& state)
{
#ifdef _DEBUG
	if (!open || !*open || !input) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(288.0f, 330.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("キー操作デバッグ", open);
	if (ImGui::CollapsingHeader(
			"Input / State Debug",
			ImGuiTreeNodeFlags_DefaultOpen)) {
		const Vector2 keyboardMove = GetKeyboardMove(input);
		const Vector2 gamepadMove = GetGamepadMove(input);
		Vector2 gamepadAim{ 0.0f, 0.0f };
		const bool rightStickAimActive =
			GameInputBindings::GetAimVector(input, gamepadAim);
		const auto mouseMove = input->GetMouseMove();
		const bool mouseAimActive =
			mouseMove.lX != 0 ||
			mouseMove.lY != 0 ||
			input->PushMouse(0) ||
			input->PushMouse(1);

		ImGui::Text(
			"Priority Device: %s",
			GameInputBindings::ToDisplayName(state.navigationDevice));
		ImGui::Text(
			"Keyboard Move: %.2f, %.2f",
			keyboardMove.x,
			keyboardMove.y);
		ImGui::Text(
			"Gamepad Move: %.2f, %.2f",
			gamepadMove.x,
			gamepadMove.y);
		ImGui::Text(
			"Mouse Aim: %s  Move: %ld, %ld  Pos: %.0f, %.0f",
			BoolText(mouseAimActive),
			mouseMove.lX,
			mouseMove.lY,
			input->GetMousePos().x,
			input->GetMousePos().y);
		ImGui::Text(
			"Right Stick Aim: %s  %.2f, %.2f",
			BoolText(rightStickAimActive),
			gamepadAim.x,
			gamepadAim.y);

		ImGui::Separator();
		ImGui::Text(
			"Scene State: %.*s  Pending Scene: %.*s",
			static_cast<int>(state.sceneState.size()),
			state.sceneState.data(),
			static_cast<int>(state.pendingScene.size()),
			state.pendingScene.data());
		ImGui::Text(
			"Gameplay Update: %s",
			BoolText(state.gameplayUpdateRuns));
		ImGui::Text(
			"Player / Enemy / Bullet / Collision / EXP Update: %s",
			BoolText(state.gameplayUpdateRuns));
		ImGui::Text("UI / Curtain / Effects Update: true");
		ImGui::Text("Paused Safety: %s", BoolText(state.pausedSafety));
		ImGui::Text("LevelUp Safety: %s", BoolText(state.levelUpSafety));
		ImGui::Text("Dead Safety: %s", BoolText(state.deadSafety));

		const Engine::Graphics3D::ModelLoadDiagnostics diagnostics =
			Engine::Graphics3D::Model::GetLoadDiagnostics();
		ImGui::Text(
			"Model Load Warnings: non-triangle skipped %u / mesh fallback %u",
			diagnostics.skippedNonTriangleFaceCount,
			diagnostics.meshFallbackCount);
	}
	ImGui::End();
#else
	(void)open;
	(void)input;
	(void)state;
#endif
}

}
