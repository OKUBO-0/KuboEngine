#include "DebugEditorManager.h"

#include "IconsFontAwesome5.h"
#include "OffscreenRenderManager.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Engine::Editor {

namespace {

void DrawMenuItems(const DebugEditorMenuItem* items, size_t count)
{
	for (size_t index = 0; index < count; ++index) {
		if (items[index].open) {
			ImGui::MenuItem(items[index].label, nullptr, items[index].open);
		}
	}
}

} // namespace

void DebugEditorManager::BuildDefaultDockLayout(unsigned int dockspaceId)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

	ImGuiID dockMain = dockspaceId;
	ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
	ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.63f, nullptr, &dockMain);
	ImGuiID dockRight = dockMain;
	ImGuiID dockRightTop = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.58f, nullptr, &dockRight);

	ImGui::DockBuilderDockWindow("Scene", dockLeft);
	ImGui::DockBuilderDockWindow("統計", dockRightTop);
	ImGui::DockBuilderDockWindow("シーン設定", dockRightTop);
	ImGui::DockBuilderDockWindow("キー操作デバッグ", dockBottom);
	ImGui::DockBuilderDockWindow("ライト設定", dockRight);
	ImGui::DockBuilderDockWindow("オーディオ", dockRight);
	ImGui::DockBuilderDockWindow("OffscreenRenderManager", dockRight);
	ImGui::DockBuilderDockWindow("シーン固有デバッグ", dockRight);
	ImGui::DockBuilderDockWindow("オブジェクトビュー / オブジェクト設定", dockRight);
	ImGui::DockBuilderDockWindow("パーティクルビュー / スプライトマネージャ", dockRight);
	ImGui::DockBuilderDockWindow("オフスクリーン設定", dockRight);
	ImGui::DockBuilderDockWindow("ギズモ", dockRight);
	ImGui::DockBuilderDockWindow("オブジェクトマネージャ", dockRight);
	ImGui::DockBuilderDockWindow("モーションエディター", dockRight);
	ImGui::DockBuilderDockWindow("コライダー/タグ管理", dockRight);
	ImGui::DockBuilderFinish(dockspaceId);
}

void DebugEditorManager::DrawMainMenu(const DebugEditorMenuConfig& config)
{
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (ImGui::BeginMenu(ICON_FA_FILE " ファイル")) {
		if (config.saveLabel && config.onSave && ImGui::MenuItem(config.saveLabel)) {
			config.onSave();
		}
		if (config.restoreLabel && config.onRestore && ImGui::MenuItem(config.restoreLabel)) {
			config.onRestore();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_EDIT " 編集")) {
		DrawMenuItems(config.editItems, config.editItemCount);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_EYE " 表示")) {
		if (ImGui::BeginMenu("ウィンドウ")) {
			DrawMenuItems(config.windowItems, config.windowItemCount);
			ImGui::EndMenu();
		}
		if (config.windowSwitcher) {
			ImGui::MenuItem("ウィンドウ表示切り替え", nullptr, config.windowSwitcher);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_CUBE " オブジェクト")) {
		DrawMenuItems(config.objectItems, config.objectItemCount);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " ヘルプ")) {
		if (config.helpText) {
			ImGui::TextUnformatted(config.helpText);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_GLOBE " シーン選択")) {
		if (config.onGoTitle && ImGui::MenuItem("タイトルへ移動")) {
			config.onGoTitle();
		}
		if (config.onGoGame && ImGui::MenuItem("ゲームへ移動")) {
			config.onGoGame();
		}
		if (config.onGoResult && ImGui::MenuItem("リザルトへ移動")) {
			config.onGoResult();
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void DebugEditorManager::DrawWindowSwitcher(
	const char* title,
	bool* open,
	const DebugEditorMenuItem* items,
	size_t itemCount,
	const Vector2& defaultSize,
	std::function<void()> footer)
{
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(defaultSize.x, defaultSize.y), ImGuiCond_FirstUseEver);
	ImGui::Begin(title, open);
	DrawMenuItems(items, itemCount);
	if (footer) {
		ImGui::Separator();
		footer();
	}
	ImGui::End();
}

DebugSceneViewportState DebugEditorManager::DrawSceneViewport(bool* open)
{
	DebugSceneViewportState state{};
	if (open && !*open) {
		return state;
	}

	ImGui::SetNextWindowPos(ImVec2(288.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	if (!ImGui::Begin("Scene", open)) {
		ImGui::End();
		ImGui::PopStyleVar();
		return state;
	}

	state.inputActive = ImGui::IsWindowHovered() || ImGui::IsWindowFocused();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance();
		offscreen && offscreen->HasImGuiSceneTexture()) {
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		constexpr float kSceneAspect = 16.0f / 9.0f;
		ImVec2 imageSize{ availableSize.x, availableSize.x / kSceneAspect };
		if (imageSize.y > availableSize.y) {
			imageSize.y = availableSize.y;
			imageSize.x = imageSize.y * kSceneAspect;
		}

		const float cursorX = ImGui::GetCursorPosX() + (availableSize.x - imageSize.x) * 0.5f;
		ImGui::SetCursorPosX(cursorX);
		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		state.drawn = imageSize.x > 0.0f && imageSize.y > 0.0f;
		state.min = { imageMin.x, imageMin.y };
		state.size = { imageSize.x, imageSize.y };

		ImGui::GetWindowDrawList()->AddRectFilled(
			imageMin,
			ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y),
			IM_COL32(0, 0, 0, 255));
		ImGui::Image(
			static_cast<ImTextureID>(offscreen->GetImGuiSceneTextureHandle().ptr),
			imageSize,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f));
	}

	ImGui::End();
	ImGui::PopStyleVar();
	return state;
}

} // namespace Engine::Editor
