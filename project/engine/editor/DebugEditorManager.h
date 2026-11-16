#pragma once

#include "Vector2.h"
#include "../base/utility/Logger.h"
#include <cstddef>
#include <functional>
#include <filesystem>
#include <cstdint>
#include <string>

namespace Engine::Assets { class AssetRegistry; class AssetImportJob; }
namespace Engine::Editor {
class AssetThumbnailCache;
inline constexpr char AssetGuidPayload[] = "KUBO_ASSET_GUID";
#if defined(KUBO_APPLICATION_CHECKS)
inline void (*assetBrowserChecksObserver)(const char*,uint64_t,Vector2,Vector2,bool)=nullptr;
inline void (*gameInputAdmissionChecksObserver)(bool, float)=nullptr;
#endif

struct DebugSceneViewportState {
	bool drawn = false;
	bool inputActive = false;
	Vector2 min{ 0.0f, 0.0f };
	Vector2 size{ 0.0f, 0.0f };
	Vector2 available{ 0.0f, 0.0f };
};

struct DebugEditorMenuItem {
	const char* label = "";
	bool* open = nullptr;
	const char* icon = "";
	const char* settingsId = nullptr;
	const char* legacyLabel = nullptr;
};

enum class DebugPlaybackState {
	Unavailable,
	Playing,
	Paused,
	Stopped,
};

enum class SceneEditAction { Undo, Redo, Duplicate, Delete };

struct DebugEditorMenuConfig {
	const DebugEditorMenuItem* windowItems = nullptr;
	size_t windowItemCount = 0;
	const DebugEditorMenuItem* editItems = nullptr;
	size_t editItemCount = 0;
	const DebugEditorMenuItem* objectItems = nullptr;
	size_t objectItemCount = 0;
	const char* saveLabel = nullptr;
	const char* saveUnavailableReason = nullptr;
	std::function<void()> onSave;
	std::string sceneName;
	std::string scenePath;
	bool sceneDirty = false;
	const char* restoreLabel = nullptr;
	std::function<void()> onRestore;
	std::function<void()> onExport;
	std::function<void()> onProjectSettings;
	std::function<void()> onOpenScene;
	bool canOpenScene = false;
	const char* openSceneUnavailableReason = nullptr;
	std::function<void()> onNewScene;
	std::function<void()> onNewController;
	bool canNewController = false;
	const char* newControllerUnavailableReason = nullptr;
	std::function<void()> onSaveSceneAs;
	bool canSaveSceneAs = false;
	std::function<void()> onReturnToHub;
	const char* helpText = nullptr;
	std::function<void()> onGoTitle;
	std::function<void()> onGoGame;
	std::function<void()> onGoResult;
	DebugPlaybackState playbackState = DebugPlaybackState::Unavailable;
	const char* playbackUnavailableReason = nullptr;
	std::function<void()> onPlay;
	std::function<void()> onPause;
	std::function<void()> onStop;
	std::function<void()> onResetLayout;
	std::function<void(bool)> onApplyWorkspace;
	std::function<void(SceneEditAction)> onSceneEdit;
	std::function<const char*(SceneEditAction)> sceneEditUnavailableReason;
};

class DebugEditorManager {
public:
	static void SetProjectRoot(const std::filesystem::path& root);
	static void BuildDefaultDockLayout(unsigned int dockspaceId, bool diagnostics = false);
	static void DiscardRetiredDockWindows();
	static void ApplyDarkEditorStyle();
	static void DrawMainMenu(const DebugEditorMenuConfig& config);
	static void SaveWindowItems(const DebugEditorMenuItem* items, size_t itemCount);
	static DebugSceneViewportState DrawSceneViewport(bool* open, std::function<void()> toolbar = {}, const char* title = "Scene", uint64_t textureHandle = 0,
		std::function<void(Vector2)> onAvailable = {}, std::function<bool(const DebugSceneViewportState&)> overlay = {});
    static void DrawConsole(bool* open,const std::function<void(const Base::Logger::Entry&)>& actions={});
	static void DrawAssetBrowser(bool* open, const std::filesystem::path& projectAssetRoot = {}, Engine::Assets::AssetRegistry* sharedRegistry = nullptr,
		Engine::Assets::AssetImportJob* sharedJob = nullptr, const char* refreshBlockedReason = nullptr, bool allowMoves = true,
        const std::filesystem::path& owningProjectRoot = {}, const std::filesystem::path& projectFile = {},
        std::function<std::string(const std::string&)> moveGuard = {}, AssetThumbnailCache* thumbnails = nullptr,
        std::function<void(const std::filesystem::path&)> openScene = {},std::string* requestedReveal=nullptr);
};

} // namespace Engine::Editor
