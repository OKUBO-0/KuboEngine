#pragma once
#include "engine/editor/ControllerPendingTransactions.h"
namespace Engine::Editor {struct ControllerRecordReport;}
#include "Framework.h"
#include "engine/scene/entity/EntityWorld.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/script/ScriptRuntime.h"
#include "engine/audio/ScriptAudioHost.h"
#include "engine/audio/EntityAudioHost.h"
#include "engine/scene/entity/WorldPlayback.h"
#include "engine/editor/EntityHistory.h"
#include "engine/editor/EntityDocumentState.h"
#include "engine/editor/AnimatorControllerPanel.h"
#include "engine/editor/EditorCameraState.h"
#include "engine/editor/GizmoSnapSettings.h"
#include "engine/editor/EntitySelection.h"
#include "engine/editor/NativeBuildSettings.h"
#include "engine/editor/PrefabPendingTransactions.h"
#include "engine/editor/PrefabOverrides.h"
#include "engine/editor/PrefabThreeWay.h"
#include "engine/editor/PrefabApplyReview.h"
#include "engine/editor/PrefabSourceHistoryRecovery.h"
#include "engine/editor/NativePrefabApplyEffect.h"
#include "engine/base/render/ViewResizeSchedule.h"
#include "engine/base/ProjectContext.h"
#include "Camera.h"
#include "engine/services/ExportJob.h"
#include "engine/services/GitWorkspace.h"
#include <optional>
#include "engine/services/EditorMcpSession.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/assets/AssetImportJob.h"
#include "engine/editor/AssetThumbnailCache.h"
#include <future>
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace Engine::Scene {
class AbstractSceneFactory;
}
namespace Engine::Editor { struct DebugSceneViewportState; struct DebugEditorMenuConfig; struct EntityCreation; enum class SceneEditAction; class ProjectPreviewCapture; }

/// @brief アプリケーション固有のシーン制御と描画順を定義するクラス
/// @details Framework を継承し、シーン生成とオフスクリーン描画の流れを構築する。
namespace Engine::Scene {

class Game : public Engine::Base::Framework {
public:
	Game();
	explicit Game(std::unique_ptr<AbstractSceneFactory> sceneFactory, std::string initialSceneName = "GAMEPLAY");
	~Game() override;

	void SetSceneFactory(std::unique_ptr<AbstractSceneFactory> sceneFactory);
	void SetInitialSceneName(std::string sceneName);
	bool ConfigureStartupScene(const std::filesystem::path& path, std::string& error);
	void SetEditorMode(bool enabled) { editorMode_ = enabled; editorToolsEnabled_ = enabled; }
	void SetHubReadyEvent(void* handle) noexcept { hubReadyEvent_ = handle; hubLaunched_ = handle != nullptr; }
	bool IsEditorMode() const { return editorMode_; }
	void SetProjectFile(std::filesystem::path path) { projectFile_ = path.empty()?std::filesystem::path{}:std::filesystem::absolute(path).lexically_normal(); }
	void SetProjectAssetRoot(std::filesystem::path path) { projectAssetRoot_ = std::move(path); }
	bool ConfigureLuaResourceLimits(Engine::Scripting::LuaResourceLimits limits,std::string& error) { return scriptRuntime_.ConfigureLuaResourceLimits(limits,error); }
	Engine::Scripting::LuaResourceLimits GetLuaResourceLimits() const noexcept { return scriptRuntime_.GetLuaResourceLimits(); }
	void SetMcpAtStartup(bool enabled) { mcpAtStartup_ = enabled; }
	void OpenProjectSettings() { ReloadProjectSettings(); projectSettingsOpen_ = true; }
	// Explicit confirmed index command; true means queued, not completed.
	bool StartGitIndexEdit(const Engine::Services::GitStatus& expected,const Engine::Services::GitChange& selected,bool stage,std::string& error);
	bool StartGitCommit(const Engine::Services::GitStatus& expected,const std::string& message,std::string& error);
	bool StartGitDiff(const Engine::Services::GitStatus& expected,const Engine::Services::GitChange& selected,bool staged,std::string& error);
	bool StartGitHistory(const Engine::Services::GitStatus& expected,size_t skip,std::string& error);
	void OpenExportWindow(bool native = false);
	bool ReloadExportSettings();
	bool SaveExportSettings();
	// Main-thread Editor commands, shared by UI and integration clients.
	bool CreateEditorEntity(const std::string& name,EntityId parent,EntityId& created,std::string& error);
	bool SaveEditorPrefab(const std::filesystem::path& path,std::filesystem::path& saved,std::string& error);
	bool InstantiateEditorPrefab(const std::string& guid,EntityId parent,EntityId& created,std::string& error,
		std::optional<Vector3> placement = std::nullopt);
	bool UnlinkEditorPrefab(EntityId root,std::string& error);
	bool RevertEditorPrefab(EntityId root,std::string& error);
	bool RequestEditorPrefabRevert(EntityId root,std::string& error);
	bool ConfirmEditorPrefabRevert(std::string& error);
	bool HasPendingPrefabRevert() const {return pendingPrefabRevertRoot_!=0;}
	void CancelEditorPrefabRevert() noexcept;
	bool InspectEditorPrefabOverrides(EntityId root,std::string& error);
	bool InspectEditorPrefabThreeWay(EntityId root,std::string& error);
	const Engine::Editor::PrefabThreeWayReport* GetEditorPrefabThreeWay() const {return prefabThreeWay_?&prefabThreeWay_->report:nullptr;}
	bool RequestEditorPrefabFieldRevert(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,std::string& error);
	bool RequestEditorPrefabFieldAdoption(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error);
	bool HasPendingPrefabFieldAdoption() const {return prefabFieldRevert_ && prefabFieldRevert_->adoption.has_value() && !prefabFieldRevert_->entityAddition && !prefabFieldRevert_->entityRemoval && !prefabFieldRevert_->entityRestoration;}
	bool RequestEditorPrefabEntityAddition(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error);
	bool HasPendingPrefabEntityAddition() const {return prefabFieldRevert_ && prefabFieldRevert_->entityAddition;}
	bool ConfirmEditorPrefabEntityAddition(std::string& error);
	bool RequestEditorPrefabEntityRemoval(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error);
	bool HasPendingPrefabEntityRemoval() const {return prefabFieldRevert_ && prefabFieldRevert_->entityRemoval;}
	const std::vector<Engine::Editor::PrefabThreeWayEntry>* GetPendingPrefabEntityRemovals() const {return HasPendingPrefabEntityRemoval()?&prefabFieldRevert_->removals:nullptr;}
	bool ConfirmEditorPrefabEntityRemoval(std::string& error,bool approveConflicts=false);
	bool RequestEditorPrefabEntityRestoration(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error);
	bool HasPendingPrefabEntityRestoration() const {return prefabFieldRevert_ && prefabFieldRevert_->entityRestoration;}
	const std::vector<Engine::Editor::PrefabThreeWayEntry>* GetPendingPrefabEntityRestorations() const {return HasPendingPrefabEntityRestoration()?&prefabFieldRevert_->restorations:nullptr;}
	bool ConfirmEditorPrefabEntityRestoration(std::string& error,bool approveConflicts=false);
	bool ConfirmEditorPrefabFieldAdoption(std::string& error,bool approveConflict=false);
	bool ConfirmEditorPrefabFieldRevert(std::string& error);
	bool ConfirmEditorPrefabFieldRevert(std::string& error,bool approveConflict);
	std::optional<Engine::Editor::PrefabThreeWayChange> GetPendingPrefabFieldRevertChange() const {return prefabFieldRevert_?std::optional(prefabFieldRevert_->comparison):std::nullopt;}
	void CancelEditorPrefabFieldRevert() {prefabFieldRevert_.reset();}
	bool HasPendingPrefabFieldRevert() const {return bool(prefabFieldRevert_);}
	const Engine::Editor::PrefabOverrideReport& GetEditorPrefabOverrides() const {return prefabOverrides_;}
	bool RequestEditorPrefabApply(EntityId root,std::string& error);
	bool RequestEditorPrefabFieldApply(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,std::string& error);
	bool ConfirmEditorPrefabApply(std::string& error);
	bool ConfirmEditorPrefabApply(std::string& error,bool approveFieldConflict);
	const Engine::Editor::PrefabApplySourceReview* GetPendingPrefabApplySourceReview() const {return prefabApply_ && !prefabApply_->field?&prefabApply_->sourceReview:nullptr;}
	std::optional<Engine::Editor::PrefabThreeWayChange> GetPendingPrefabFieldApplyChange() const {return prefabApply_ && prefabApply_->field?std::optional(prefabApply_->comparison):std::nullopt;}
	void CancelEditorPrefabApply() {prefabApply_.reset();}
	bool HasPendingPrefabApply() const {return bool(prefabApply_);}
	bool RefreshPrefabRecovery(std::string& error);
	bool InspectEditorPrefabRecovery(const std::string& guid,std::string& error);
	bool RestoreEditorPrefabRecovery(std::string& error);
	bool CompleteEditorPrefabRecovery(std::string& error);
	bool HasPendingPrefabRecovery() const {return prefabRecoveryBlocked_;}
	bool RequestOpenScene(const std::filesystem::path& path, std::string& error);
	bool RequestNewScene(std::string& error);
	bool AlignEditorSelectionToWorldGrid(float step, std::string& error);
	bool SelectEditorEntity(EntityId id,bool additive,bool range,std::string& error);
	bool DuplicateEditorSelection(std::string& error);
	bool DeleteEditorSelection(std::string& error);
	bool ReparentEditorSelection(EntityId parent,bool keepWorld,std::string& error);
	bool TransformEditorSelection(const Matrix4x4& from,const Matrix4x4& to,int operation,std::string& error);
	bool ApplyEditorHistory(bool redo,std::string& error);
	bool PlayEditorScene(std::string& error);
	const std::vector<EntityId>& GetEditorSelection() { editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);return editorSelection_.Ids(); }
	bool SaveSceneAs(const std::filesystem::path& path, std::string& error, bool overwriteApproved=false);
	enum class DocumentDecision { Save, Discard, Cancel };
	bool ConfirmDocumentAction(DocumentDecision decision, std::string& error);
    bool OpenControllerAssetEditor(const std::string& guid,std::string& error);
    bool ConfirmControllerClose(bool discard,std::string& error);
    bool ConfirmControllerClose(DocumentDecision decision,std::string& error);
    bool SaveControllerAsset(std::string& error);
    bool SaveControllerAssetAs(const std::string& assetRelative,std::string& error);
    bool ReloadSelectedController(std::string& error);
    bool NewControllerAssetEditor(std::string& error);
    bool InspectControllerRecovery(Engine::Editor::ControllerPendingFamily family,const std::string& guid,std::string& error);
    bool RestoreControllerRecovery(std::string& error);
    bool CompleteControllerRecovery(std::string& error);
    bool InspectControllerRecord(Engine::Editor::ControllerPendingFamily family,const std::string& guid,std::string& error,const std::filesystem::path& archive={});
    bool QuarantineControllerRecord(std::string& error);
    bool RecoverControllerDraft(std::string& error);
    bool RequestControllerRecordAction(bool quarantine,std::string& error);
    bool ConfirmControllerRecordAction(bool approved,std::string& error);
	const std::filesystem::path& GetOpenScenePath() const { return documentState_.Path(); }
	bool HasPendingDocumentAction() const { return documentState_.HasPendingAction(); }
	bool IsSceneDirty() const { return documentState_.IsDirty(); }

	/// @brief ゲーム固有の初期化を行う
	/// @param なし
	/// @return なし
	void Initialize() override;
	/// @brief ゲーム固有の終了処理を行う
	/// @param なし
	/// @return なし
	void Finalize() override;
	/// @brief ゲーム全体の更新処理を行う
	/// @param なし
	/// @return なし
	void Update() override;
	/// @brief シーン描画と最終合成を行う
	/// @param なし
	/// @return なし
	void Draw() override;

private:
	void* hubReadyEvent_ = nullptr; // Borrowed, signaled once after a completed frame.
	bool hubShowAfterFirstFrame_ = false;
	bool hubLaunched_ = false;
	std::unique_ptr<Engine::Editor::ProjectPreviewCapture> hubPreview_;
	bool hubPreviewInitialCheck_ = true;
	bool hubPreviewRequested_ = false;
	bool hubPreviewCloseFrame_ = false;
	ULONGLONG hubPreviewInitialAt_ = 0;
	ULONGLONG hubPreviewNextAt_ = 0;
	void ConfigureHubWindowStartup();
	void FinishHubWindow();
	void RetireHubPreview();
	void CaptureHubPreview();
	void NotifyHubAfterFrame();
#if defined(KUBO_APPLICATION_CHECKS)
	friend class GameApplicationChecksAccess;
	friend class GameRenderInspectorChecksAccess;
	friend class GameThumbnailChecksAccess;
#endif
	bool ReadPendingPrefabs(std::vector<Engine::Editor::PrefabPendingEntry>& entries,std::string& error) const;
	bool RefreshHistoryRecovery(std::string& error) noexcept;
	bool CheckPrefabRecovery(std::string& error) const;
	bool CanRecoverPrefab(std::string& error) const;
	void DrawPrefabRecoveryWindow();
	void DrawControllerRecoveryWindow();
	bool RefreshControllerInventory(std::string& error);
	std::vector<Engine::Editor::ControllerPendingEntry> controllerPendingEntries_;
	std::vector<Engine::Editor::ControllerArchiveEntry> controllerArchiveEntries_;
	std::string controllerInventoryError_,controllerInspectStatus_;
	bool controllerRecoveryWindowOpen_=false;
    uint64_t controllerRecoveryFrames_=0;
    struct ControllerRecoverySelection {Engine::Editor::ControllerPendingFamily family;std::string guid;Engine::Editor::ControllerRecoveryReport publication;Engine::Editor::ControllerCreationReport creation;};
    std::unique_ptr<ControllerRecoverySelection> controllerRecoverySelection_;
    std::shared_ptr<Engine::Editor::ControllerRecordReport> controllerRecordSelection_;
    std::shared_ptr<Engine::Editor::ControllerRecordReport> controllerRecordConfirmSelection_;
    bool controllerRecordArchived_=false;
    unsigned int controllerRecordConfirmAction_=0;
    bool CanRecoverController(std::string& error) const;
    bool controllerRecoveryConfirm_=false,controllerRecoveryConfirmArchive_=false;
	void DrawPrefabOverridesWindow();
	void DrawPrefabThreeWayWindow();
	bool RequestEditorPrefabFieldRevertInternal(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,const Engine::Editor::PrefabThreeWayEntry* adoption,const std::string* expectedGuid,std::string& error,bool entityAddition=false,bool entityRemoval=false,bool entityRestoration=false);
	bool ConfirmEditorPrefabFieldRevertInternal(std::string& error,bool approveConflict,bool entityAddition,bool entityRemoval=false,bool entityRestoration=false);
	bool RequestEditorPrefabApplyInternal(EntityId root,const Engine::Editor::PrefabOverrideEntry* entry,std::string& error);
	bool CanAuthorPrefab(std::string& error) const;
	bool OpenPrefabProject(Engine::Base::ProjectContext& project,std::string& error) const;
	void DrawDebugEditorShell();
	void ConfigureEditorMenuCommands(Engine::Editor::DebugEditorMenuConfig& menu);
	const char* EditorDocumentBusyReason() const;
	const char* SceneEditUnavailableReason(Engine::Editor::SceneEditAction action) const;
	bool RunSceneEditAction(Engine::Editor::SceneEditAction action);
	bool SelectEditorEntityFromRows(EntityId id,bool additive,bool range,std::string& error,std::span<const EntityId> rows);
	enum class HierarchyType { All, Model, Camera, Light, Canvas, Sprite, Particle, Audio, Script, Physics, Prefab };
	bool HasHierarchyFilter() const;
	bool MatchesHierarchyFilter(const EntityRecord& entity) const;
	bool HasHierarchyHiddenSelection() const;
	bool DrawHierarchyTree();
	void DrawEditorCameraToolbar();
	void DrawAddComponentMenu(const EntityRecord& entity);
	void DrawInspectorAssetDropTarget(const EntityRecord& entity);
	bool CreateLuaScriptAsset(const std::filesystem::path& path,std::string& error);
	void DrawSceneGuides(const Engine::Editor::DebugSceneViewportState& viewport);
	bool DrawSceneAssetDropTarget(const Engine::Editor::DebugSceneViewportState& viewport);
	bool PlaceSceneAsset(const std::string& guid,Vector3 position,std::string& error);
	bool CreateEditorEntityWithInitial(const std::string& name,EntityId parent,EntityId& created,
		std::string& error,const Engine::Editor::EntityCreation& initial);
	void UpdateEditorViewCamera();
	void FocusEditorSelection();
	bool DrawEntityGizmo(const Engine::Editor::DebugSceneViewportState& viewport);
	void DrawExportWindow();
	void DrawProjectSettingsWindow();
	void DrawProjectInputSettings();
	void ReloadProjectSettings();
	void SelectStartupScene(const std::filesystem::path& path);
	void DrawGitWindow();
	void DrawMcpWindow();
	void InitializeGenericEditorWorld();
	void DrawGenericEntityEditor();
	void SynchronizeGenericPlayer();
	bool SaveGenericDocument();
	void ReloadGenericDocument();
	void RequestDocumentAction(Engine::Editor::EntityDocumentState::Action action);
	void DrawDocumentConfirmation();
	void BrowseOpenScene();
	bool CanOpenScene() const;
	bool OpenGenericDocument(const std::filesystem::path& path, std::string& error);
	bool NewGenericDocument(std::string& error);
	void CommitGenericDocument(Engine::Scene::EntityWorld candidate, const std::filesystem::path& path);
	bool BrowseSaveSceneAs();
	bool CanSaveSceneAs() const;

	std::string initialSceneName_;
	bool debugSceneViewOpen_ = true;
	bool debugGameViewOpen_ = true;
	bool gameScriptInputAllowed_ = false;
	bool editorViewEnabled_ = true;
	bool editorGridVisible_ = true;
	bool editorSelectionGuides_ = false;
	bool editorComponentGuides_ = false;
	void ResetEditorGridDefault();
	bool editorPickPending_ = false;
	bool editorPickAdditive_ = false;
	bool editorFocusPending_ = false;
	float editorPickX_ = 0, editorPickY_ = 0;
	Engine::Editor::EditorCameraState editorCameraState_;
	Engine::CameraSystem::Camera editorViewCamera_;
	Engine::Base::ViewResizeSchedule sceneResizeSchedule_;
	bool gizmoEnabled_ = true, gizmoLocal_ = true, gizmoDragActive_ = false;
	Matrix4x4 groupGizmoMatrix_{};
	bool groupGizmoPivotValid_=false;
	int gizmoOperation_ = 0;
	Engine::Editor::GizmoSnapSettings gizmoSnap_;
	bool debugStatsOpen_ = true;
	bool debugKeyInputOpen_ = true;
	bool openProjectInputSettings_ = false;
	bool debugOffscreenOpen_ = true;
	bool debugAudioOpen_ = false;
	bool debugConsoleOpen_ = false;
	bool debugAssetBrowserOpen_ = false;
	float debugMasterVolume_ = 1.0f;
	#ifdef _DEBUG
	bool editorMode_ = true;
	#else
	bool editorMode_ = false;
	#endif
    std::string CheckAssetMove(const std::string& guid) const;
    Engine::Scene::EntityWorld editorWorld_;
	Engine::Physics::PhysicsWorld physicsWorld_;
	std::string lastPhysicsDiagnostic_;
	Engine::Scripting::ScriptRuntime scriptRuntime_;
	Engine::AudioSystem::ScriptAudioHost scriptAudio_;
	Engine::AudioSystem::EntityAudioHost entityAudio_;
	Engine::Scene::WorldPlayback worldPlayback_;
	Engine::Editor::EntityHistory entityHistory_;
	Engine::Editor::EntityDocumentState documentState_;
	Engine::Scene::EntityId authoredSelection_ = 0;
	Engine::Scene::EntityId selectedEditorEntity_ = 0;
	Engine::Editor::EntitySelection editorSelection_;
	char hierarchySearch_[128]{};
	HierarchyType hierarchyType_ = HierarchyType::All;
	EntityId hierarchyPressedEntity_ = 0;
	bool genericHierarchyOpen_ = true;
	bool genericInspectorOpen_ = true;
	bool keepWorldOnReparent_ = false; // Transient preference; legacy default retains local TRS.
	bool genericWorldReady_ = false;
	bool genericSceneSelected_ = false;
	Engine::Scene::EntityId genericPlayerEntity_ = 0;
	std::filesystem::path genericWorldPath_ = "UserScenes/editor_world.json";
	std::string genericWorldStatus_;
    Engine::Editor::AnimatorControllerPanel controllerEditor_;
    bool controllerClosePending_=false;
	bool sampleClosePending_ = false;
    float controllerDebugBlendSeconds_=0.2f; // Runtime Inspector preference, not Scene data.
	EntityId pendingPrefabRevertRoot_=0;
	bool prefabOverridesOpen_=false;
	Engine::Editor::PrefabOverrideReport prefabOverrides_;
	std::filesystem::path prefabOverridesScene_;
	uint64_t prefabOverridesGeneration_=0;
	std::string prefabOverridesStatus_;
	struct PrefabThreeWaySnapshot {
		Engine::Editor::PrefabThreeWayReport report;
		Engine::Editor::ProjectPrefabRevision revision;
		std::filesystem::path scene;
		uint64_t generation=0;
	};
	std::unique_ptr<PrefabThreeWaySnapshot> prefabThreeWay_;
	bool prefabThreeWayOpen_=false;
	int prefabThreeWayFilter_=0,prefabThreeWayPage_=0;
	std::string prefabThreeWayStatus_;
	std::string pendingPrefabRevertWorld_;
	std::filesystem::path pendingPrefabRevertScene_;
	uint64_t pendingPrefabRevertGeneration_=0;
	struct PrefabApplyConfirmation {
		EntityId root=0;
		EntityId primary=0;
		std::optional<Engine::Editor::PrefabOverrideEntry> field;
		std::optional<std::string> sourceValue;
		Engine::Editor::PrefabThreeWayChange comparison=Engine::Editor::PrefabThreeWayChange::Unchanged;
		bool conflictApproved=false;
		Engine::Editor::PrefabApplySourceReview sourceReview;
		int reviewPage=0;
		std::filesystem::path scene;
		Engine::Editor::ProjectPrefabRevision revision;
		Engine::Editor::PrefabApplyAdmission admission;
		std::shared_ptr<Engine::Editor::NativePrefabApplyEffect> effect;
		std::vector<Engine::Editor::PrefabInstanceUpdateReport> instances;
	};
	std::unique_ptr<PrefabApplyConfirmation> prefabApply_;
	struct PrefabFieldRevertConfirmation {
		EntityId root=0,highWater=0,primary=0;
		Engine::Editor::ProjectPrefabRevision revision;
		Engine::Editor::PrefabOverrideEntry entry;
		std::optional<Engine::Editor::PrefabThreeWayEntry> adoption;
		bool entityAddition=false;
		bool entityRemoval=false;
		bool entityRestoration=false;
		std::vector<Engine::Editor::PrefabThreeWayEntry> restorations;
		int removalPage=0;
		std::vector<Engine::Editor::PrefabThreeWayEntry> removals;
		Engine::Editor::EntitySelection nextSelection;
		std::vector<EntityId> addedEntities;
		std::optional<std::string> sourceValue;
		Engine::Editor::PrefabThreeWayChange comparison=Engine::Editor::PrefabThreeWayChange::Unchanged;
		bool conflictApproved=false;
		std::string beforeWorld;
		Engine::Editor::EntitySelection::Snapshot beforeSelection;
		Engine::Editor::EntityHistory beforeHistory,nextHistory;
		Engine::Editor::EntityDocumentState beforeDocument;
		EntityWorld nextWorld;
		bool nextDirty=false;
	};
	std::unique_ptr<PrefabFieldRevertConfirmation> prefabFieldRevert_;
	std::filesystem::path projectFile_;
	bool prefabRecoveryBlocked_=false,prefabRecoveryWindowOpen_=false,prefabRecoveryInspected_=false,prefabRecoveryHistory_=false;
	std::vector<Engine::Editor::PrefabPendingEntry> prefabPending_;
	Engine::Editor::PrefabSourceHistoryRecoveryReport prefabHistoryRecovery_;
	Engine::Editor::PrefabSourceRecoveryReport prefabSourceRecovery_;
	std::string prefabRecoveryStatus_;
	Engine::Base::ProjectContext projectSettingsContext_;
	bool projectSettingsOpen_ = false;
	int projectOutputDraft_[2] = {1280,720};
	int projectLuaDraft_[3] = {64*1024*1024,256,2000000};
	char projectStartupDraft_[4097]{};
	std::string projectSettingsStatus_;
	std::filesystem::path projectAssetRoot_;
	Engine::Assets::AssetRegistry projectAssets_;
	Engine::Assets::AssetImportJob assetImportJob_;
	Engine::Editor::AssetThumbnailCache assetThumbnails_;
	void PrepareAssetThumbnails();
	Engine::Services::ExportJob exportJob_;
	bool exportWindowOpen_ = false;
	char exportDirectory_[1024] = "Builds/Export";
	bool exportNative_ = false, exportNativeDebug_ = false;
	char exportSdkDirectory_[4097]{}, exportNativeProject_[4097]{};
	std::string exportStatus_, exportLog_;
	Engine::Editor::NativeBuildSettingsSnapshot exportSettingsSnapshot_;
	bool gitWindowOpen_ = false, gitHasResult_ = false;
	Engine::Services::GitStatus gitStatus_;
	std::future<Engine::Services::GitStatus> gitRequest_;
	struct GitIndexResult { bool success=false;std::string message;Engine::Services::GitStatus status; };
	struct GitIndexSelection { Engine::Services::GitStatus expected;Engine::Services::GitChange selected;bool stage=true; };
	std::future<GitIndexResult> gitIndexRequest_;
	std::optional<GitIndexSelection> gitIndexSelection_;
	struct GitCommitSelection { Engine::Services::GitStatus expected;std::string message; };
	std::optional<GitCommitSelection> gitCommitSelection_;
	char gitCommitDraft_[4097]{};
	std::string gitIndexMessage_;
	struct GitDiffResult { std::string display,error,label; };
	std::future<GitDiffResult> gitDiffRequest_;
	GitDiffResult gitDiff_;
	bool gitDiffOpen_=false;
	struct GitHistoryResult { Engine::Services::GitHistory history;std::string error; };
	std::future<GitHistoryResult> gitHistoryRequest_;
	GitHistoryResult gitHistory_;
	Engine::Services::GitStatus gitHistoryExpected_;
	bool gitHistoryOpen_=false;
	Engine::Services::EditorMcpSession mcpSession_;
	bool mcpWindowOpen_ = false;
	std::string mcpStatus_;
	bool mcpAtStartup_ = false;
	// Host-thread admission protects live callbacks and borrowed Scene/World/UI
	// ownership until the outer teardown has finished or admitted an explicit retry.
	bool gameFinalizing_ = false;
};

}
