#include "Game.h"
#include "engine/editor/EntityCreate.h"
#include "engine/editor/RenderComponentInspector.h"
#include "engine/editor/UIComponentInspector.h"
#include "engine/editor/ParticleComponentInspector.h"
#include "engine/editor/AnimatorControllerSelection.h"
#include "engine/editor/ProjectPrefab.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/PrefabRevert.h"
#include "engine/editor/PrefabFieldRevert.h"
#include "engine/editor/PrefabFieldAdoption.h"
#include "engine/editor/PrefabEntityAddition.h"
#include "engine/editor/PrefabEntityRemoval.h"
#include "engine/editor/PrefabEntityRestoration.h"
#include "GamePlayScene.h"
#include "../entity/EntityScene.h"
#include "../entity/RuntimeSceneTransition.h"
#include "engine/physics/ColliderGeometry.h"
#include "engine/assets/ModelReference.h"
#include "engine/assets/AssetSceneDependencies.h"
#include "engine/script/ScriptReference.h"
#include "engine/editor/ProjectStartupScene.h"
#include "engine/editor/ProjectSceneSave.h"
#include "engine/editor/NativeBuildSettings.h"
#include "engine/base/ProjectPaths.h"
#include "engine/base/AtomicFile.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "Logger.h"
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
#include "Audio.h"
#include "DebugEditorManager.h"
#include "Input.h"
#include "engine/editor/ImGuizmoManager.h"
#include "engine/editor/EntityGizmoTransform.h"
#include "engine/editor/EntityGridAlignment.h"
#include "engine/editor/EntityBatchDuplicate.h"
#include "engine/editor/EntityBatchDelete.h"
#include "engine/editor/EntityBatchReparent.h"
#include "engine/editor/EntityGroupTransform.h"
#include "engine/editor/DocumentShortcuts.h"
#include "engine/editor/SceneGuideProjection.h"
#include "IconsFontAwesome5.h"
#include <commdlg.h>
#pragma comment(lib,"comdlg32.lib")
#include <imgui.h>
#endif // _DEBUG
#include <utility>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace Engine::Scene {

std::string Game::CheckAssetMove(const std::string& guid) const
{
    try{return Assets::OpenWorldAssetMoveRisk(editorWorld_,projectAssets_,GetProjectRoot(),guid);}
    catch(const std::exception& error){return std::string("Open Scene asset move inspection failed: ")+error.what();}
}

void Game::SetSceneFactory(std::unique_ptr<AbstractSceneFactory> sceneFactory)
{
	this->sceneFactory = std::move(sceneFactory);
}

void Game::SetInitialSceneName(std::string sceneName)
{
	initialSceneName_ = std::move(sceneName);
}

bool Game::ConfigureStartupScene(const std::filesystem::path& path, std::string& error)
{
	try {
		// Existing sample projects may not have saved their first scene yet.
		if(path.empty()) {error.clear();return true;}
		const auto absolute=std::filesystem::absolute(path).lexically_normal();
		if (!std::filesystem::exists(absolute)) {
			auto factory=std::make_unique<SceneFactory>(GetProjectRoot());factory->SetGameplayScenePath(absolute);
			SetSceneFactory(std::move(factory));genericSceneSelected_=false;error.clear();return true;
		}
		SceneValue document;
		if(!SceneDocument::Read(absolute,document,error)) return false;
		if (document.contains("entities")) {
			EntityWorld checked;
			if (!checked.Restore(document, error)) return false;
			genericWorldPath_ = absolute;
			genericSceneSelected_ = true;
		} else {
			auto factory = std::make_unique<SceneFactory>(GetProjectRoot());
			factory->SetGameplayScenePath(absolute);
			SetSceneFactory(std::move(factory));
			genericSceneSelected_=false;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}

void Game::Initialize()
{
    physicsWorld_.ResetSimulation();lastPhysicsDiagnostic_.clear();
	scriptRuntime_.ResetInput();
	if (genericWorldPath_.is_relative()) genericWorldPath_ = GetProjectRoot() / genericWorldPath_;
	ConfigureWindowCloseRequestPolicy(editorMode_);
	ConfigureHubWindowStartup();
	// 初期化
	Engine::Base::Framework::Initialize();
	FinishHubWindow();
	if (editorMode_ && genericSceneSelected_) offscreenRenderManager->InitializeEditorView();
	winApp->SetCloseRequestDeferred(editorMode_);
	if (!sceneFactory) {
		sceneFactory = std::make_unique<SceneFactory>(GetProjectRoot());
	}
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory.get());
	if(editorMode_){std::string error;RefreshPrefabRecovery(error);}
    if(editorMode_){std::string error;RefreshControllerInventory(error);}
	if (!projectAssetRoot_.empty()) {
		std::string error;
		if (!projectAssets_.Scan(projectAssetRoot_, error, Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly))
			genericWorldStatus_ = "Asset scan failed: " + error;
	}

	scriptRuntime_.SetAssetOwner(projectAssets_,GetProjectRoot());
	// シーンの変更
	// "TITLE"
	// "GAMEPLAY"
	// "GAMEOVER"
	// "GAMECLEAR"
	if (genericSceneSelected_) {
		std::string error;
		if (!editorWorld_.Load(genericWorldPath_, error)) throw std::runtime_error(error);
		ResetEditorGridDefault();
		documentState_.Opened(editorWorld_, genericWorldPath_);
		genericWorldReady_ = true;
		selectedEditorEntity_ = editorWorld_.GetEntities().empty() ? 0 : editorWorld_.GetEntities().front().id;
		editorSelection_.Replace(selectedEditorEntity_);
		SceneManager::GetInstance()->SetNextScene(std::make_unique<EntityScene>(editorWorld_, *dxCommon, projectAssets_, GetProjectRoot(),projectFile_));
		if (editorMode_ && mcpAtStartup_ && !projectFile_.empty()) {
			if (mcpSession_.Start(projectFile_.parent_path(), mcpStatus_)) mcpStatus_ = "MCP enabled by explicit --enable-mcp option";
		}
	} else {
		SceneManager::GetInstance()->ChangeScene(initialSceneName_);
	}
}

void Game::Finalize()
{
	if (gameFinalizing_) return;
	if (scriptRuntime_.IsDispatchActive()) throw std::logic_error("Cannot finalize Editor during script dispatch");
	gameFinalizing_ = true;
	struct Guard { bool& flag; ~Guard() { flag = false; } } guard{gameFinalizing_};
	// A failed GPU wait must keep every derived owner live, before jobs, MCP or
	// script hooks release resources. Recorded commands retire without submission.
	if (dxCommon) dxCommon->PrepareForShutdown();
	RetireHubPreview();
	assetThumbnails_.ReleaseAfterGpuIdle();
	assetImportJob_.CancelAndWait();
	mcpSession_.Stop();
	{Engine::Scripting::ScriptRuntime::AnimatorReadScope animatorRead(scriptRuntime_,dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()));scriptRuntime_.Shutdown(editorWorld_);}
	scriptAudio_.Clear();
	entityAudio_.Clear();
	// 終了
	{std::string layoutError;if(!controllerEditor_.FlushLayout(layoutError))Engine::Base::Logger::Log("Controller layout not saved on shutdown: "+layoutError,Engine::Base::Logger::Level::Warning);}
	controllerEditor_.RetireGui();
	Engine::Base::Framework::Finalize();
}

void Game::Update()
{
	editorPickPending_ = false;
	if(editorMode_){std::string error;RefreshPrefabRecovery(error);}
    if(editorMode_){std::string error;RefreshControllerInventory(error);}
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (editorMode_) {
		PrepareAssetThumbnails();
		imGuiManager->Begin();
	}
#endif // _DEBUG
	// 更新
	Engine::Base::Framework::Update();
	if (!genericWorldReady_ && (editorMode_ || std::filesystem::is_regular_file(genericWorldPath_))) {
		InitializeGenericEditorWorld();
		genericWorldReady_ = true;
	}
	if (editorMode_ && winApp->ConsumeCloseRequest()) RequestDocumentAction(Engine::Editor::EntityDocumentState::Action::Close);
	gameScriptInputAllowed_ = false;
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	const bool drawEditorBeforeSimulation = editorMode_ && genericSceneSelected_;
	float editorWheel = 0.0f;
	if (drawEditorBeforeSimulation) {
		DrawDebugEditorShell();
		// ImGui clears this frame's wheel delta when Render ends the frame.
		editorWheel = ImGui::GetIO().MouseWheel;
		imGuiManager->End();
	}
#endif
	Engine::Scripting::ScriptRuntime::AnimatorReadScope animatorRead(scriptRuntime_,dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()));
	const float genericDelta = editorMode_ ? (prefabRecoveryBlocked_?0.0f:worldPlayback_.SimulationDelta(frameDeltaTime_)) : frameDeltaTime_;
	scriptRuntime_.SetSceneTransitionsEnabled(genericSceneSelected_ && !projectAssetRoot_.empty() &&
		(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing));
	if (genericDelta>0 && !documentState_.HasPendingAction()) {
		if (auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
			if (const auto request=scriptRuntime_.TakeReadySceneTransition(genericDelta)) {
				EntityWorld next;
				std::filesystem::path destination;
				std::string error;
				if (LoadRuntimeScene(*request,GetProjectRoot(),projectAssetRoot_,projectAssets_,next,destination,error)) {
					scriptRuntime_.Shutdown(editorWorld_);
					scriptAudio_.Clear();
					entityAudio_.Clear();
					physicsWorld_.ResetSimulation();
					scene->ResetWorldRenderState();
					editorWorld_.Swap(next);
					runtimeSceneChanged_ = true;
					selectedEditorEntity_ = 0;
					editorSelection_.Replace(0);
					editorPickPending_ = false;
					genericWorldStatus_ = "Playing Scene: " + Base::PathToUtf8(destination.filename());
				} else {
					genericWorldStatus_ = "Scene transition failed: " + error;
					Base::Logger::Log(genericWorldStatus_,Base::Logger::Level::Error);
				}
			}
		}
	}
	scriptRuntime_.SetAudioRequestsEnabled(genericWorldReady_ && (!editorMode_ || (!prefabRecoveryBlocked_ && worldPlayback_.GetState()!=WorldPlayback::State::Editing)));
	scriptRuntime_.SetAnimatorRequestsEnabled(genericWorldReady_ && dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()) && (!editorMode_ || (!prefabRecoveryBlocked_ && worldPlayback_.GetState()!=WorldPlayback::State::Editing)));
	if (genericWorldReady_ && genericDelta > 0.0f) {
		bool scriptInputAllowed = !editorMode_;
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
		if (editorMode_) scriptInputAllowed = gameScriptInputAllowed_ && !documentState_.HasPendingAction() &&
			!gizmoDragActive_ && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
#if defined(KUBO_APPLICATION_CHECKS)
		if (Engine::Editor::gameInputAdmissionChecksObserver)
			Engine::Editor::gameInputAdmissionChecksObserver(scriptInputAllowed, scriptInputAllowed ? editorWheel : 0.0f);
#endif
#endif
		auto inputFrame = Engine::InputSystem::Input::GetInstance()->CaptureFrame(scriptInputAllowed);
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
		// The Editor's Win32 backend consumes wheel events once per ImGui frame.
		if (editorMode_ && inputFrame.available) inputFrame.wheel = editorWheel;
#endif
		scriptRuntime_.SetInputFrame(std::move(inputFrame));
		std::string scriptError;
		if (!Engine::Scripting::LoadScriptWorldAssets(scriptRuntime_, editorWorld_, GetProjectRoot(), projectAssets_, scriptError)) {
			genericWorldStatus_ = "Script load failed: " + scriptError;
		}
		auto scriptFrame=scriptRuntime_.BeginFrame();
		scriptRuntime_.Update(editorWorld_, genericDelta);
		physicsWorld_.Advance(editorWorld_,genericDelta,[this](float dt){scriptRuntime_.FixedUpdate(editorWorld_,dt);},
			[this](std::span<const Physics::TriggerEvent> events){scriptRuntime_.DispatchTriggers(editorWorld_,events);});
	}
	else if(genericWorldReady_){
		physicsWorld_.Advance(editorWorld_,0);
		// A zero host delta is not a Pause. Preserve queued input while Playing.
		if(editorMode_ && (prefabRecoveryBlocked_ || worldPlayback_.GetState()!=WorldPlayback::State::Playing))scriptRuntime_.ResetInput();
	}
	if (genericWorldReady_) {
		const auto& physicsDiagnostic=physicsWorld_.GetDiagnostic();
		if(!physicsDiagnostic.empty() && physicsDiagnostic!=lastPhysicsDiagnostic_)
			Engine::Base::Logger::Log("Editor physics: "+physicsDiagnostic,Engine::Base::Logger::Level::Error);
		lastPhysicsDiagnostic_=physicsDiagnostic;
		if(genericDelta>0)SynchronizeGenericPlayer();
	}
	if (editorMode_ && genericSceneSelected_ && genericWorldReady_) {
		const bool editing = worldPlayback_.GetState() == WorldPlayback::State::Editing && !EditorDocumentBusyReason();
		mcpSession_.Tick(editorWorld_, editing, &entityHistory_, selectedEditorEntity_,
			[this](const std::filesystem::path& path, const EntityWorld& world) { documentState_.MarkSavedAt(world, path); },
			&editorSelection_, &projectAssets_, [this, editing] {
				SceneValue status;
				const auto playback = worldPlayback_.GetState();
				const auto& path = documentState_.Path();
				status["scenePath"] = path.empty() ? std::string{} : Engine::Base::PathToUtf8(path.lexically_relative(GetProjectRoot()));
				status["playback"] = playback == WorldPlayback::State::Playing ? "playing" :
					playback == WorldPlayback::State::Paused ? "paused" : "editing";
				status["dirty"] = playback == WorldPlayback::State::Editing
					? documentState_.WouldBeDirty(editorWorld_) : documentState_.IsDirty();
				status["canEditScene"] = editing;
				const char* reason = EditorDocumentBusyReason();
				status["editBlockedReason"] = editing ? "" : reason ? reason : "Stop playback to edit the Scene.";
				status["primaryEntityId"] = double(editorSelection_.Primary());
				status["selectionCount"] = double(editorSelection_.Ids().size());
				status["sceneGridEnabled"] = editorViewEnabled_ && editorGridVisible_;
				status["transformGizmoEnabled"] = editorViewEnabled_ && gizmoEnabled_;
				return status;
			});
		selectedEditorEntity_=editorSelection_.Primary();
		editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	}

#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (editorMode_ && !drawEditorBeforeSimulation) {
		DrawDebugEditorShell();
		imGuiManager->End();
	}
#endif // _DEBUG
	scriptAudio_.Synchronize(scriptRuntime_,genericWorldReady_ && (!editorMode_ || (!prefabRecoveryBlocked_ && worldPlayback_.GetState()!=WorldPlayback::State::Editing)),
		editorMode_ && worldPlayback_.GetState()==WorldPlayback::State::Paused,
		[](uint64_t id,const std::string& error) {Engine::Base::Logger::Log("Editor audio Entity "+std::to_string(id)+": "+error,Engine::Base::Logger::Level::Error);});
	if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
		entityAudio_.Synchronize(editorWorld_,projectAssets_,GetProjectRoot(),genericWorldReady_ && (!editorMode_ || (!prefabRecoveryBlocked_ && worldPlayback_.GetState()!=WorldPlayback::State::Editing)),
			editorMode_ && worldPlayback_.GetState()==WorldPlayback::State::Paused,
			[](uint64_t id,const std::string& error) {Engine::Base::Logger::Log("Editor AudioSource Entity "+std::to_string(id)+": "+error,Engine::Base::Logger::Level::Error);});
		scene->SetSceneAspectRatio(editorMode_ && offscreenRenderManager->HasEditorView() ? offscreenRenderManager->GetEditorViewExtent().Aspect() : GetGameOutputExtent().Aspect());
		scene->SetUIOutputExtent(GetGameOutputExtent());
		UpdateEditorViewCamera();
		scene->SetEditorCamera(editorMode_ && editorViewEnabled_ ? &editorViewCamera_ : nullptr);
		const auto animatorRequests=scriptRuntime_.TakeAnimatorRequests();
		if(animatorRequests.count){scene->SynchronizeModels(0);scene->ApplyAnimatorRequests(animatorRequests);}
		scene->SynchronizeModels(editorMode_ ? (prefabRecoveryBlocked_?0.0f:worldPlayback_.SimulationDelta(frameDeltaTime_)) : frameDeltaTime_);
        scene->UpdateParticles(editorMode_?worldPlayback_.SimulationDelta(frameDeltaTime_):frameDeltaTime_,
            genericWorldReady_&&(!editorMode_||worldPlayback_.GetState()!=WorldPlayback::State::Editing),
            prefabRecoveryBlocked_||(editorMode_&&worldPlayback_.GetState()==WorldPlayback::State::Paused));
		if (editorPickPending_ && editorMode_ && editorViewEnabled_) {
			Engine::Editor::ScenePickSegment segment;
			if (Engine::Editor::MakeScenePickSegment(editorPickX_, editorPickY_, editorViewCamera_.GetViewProjectionMatrix(), segment)) {
				std::string error;SelectEditorEntity(scene->PickModel(segment),editorPickAdditive_,false,error);
			}
		}
	}
}

void Game::DrawDebugEditorShell()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	DrawPrefabRecoveryWindow();
    DrawControllerRecoveryWindow();
	DrawPrefabOverridesWindow();
	DrawPrefabThreeWayWindow();
	DrawGenericEntityEditor();
	controllerEditor_.Draw(worldPlayback_.GetState()==WorldPlayback::State::Editing && !assetImportJob_.IsBusy() && exportJob_.GetState()!=Engine::Services::ExportJob::State::Running && !controllerClosePending_ && !documentState_.HasPendingAction() && !prefabRecoveryBlocked_ && !gizmoDragActive_,[this](std::string& error){return SaveControllerAsset(error);},[this](const std::string& path,std::string& error){return SaveControllerAssetAs(path,error);},[this](std::string& error){return ReloadSelectedController(error);});
	DrawDocumentConfirmation();
	if (endRequest_) return;
	Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &debugSceneViewOpen_, "", "view.scene" },
		{ "Game", &debugGameViewOpen_, "", "view.game" },
		{ "Entity Hierarchy", &genericHierarchyOpen_, "", "view.hierarchy" },
		{ "Entity Inspector", &genericInspectorOpen_, "", "view.inspector" },
		{ "Console", &debugConsoleOpen_, "", "view.console" },
		{ "Asset Browser", &debugAssetBrowserOpen_, "", "view.assets" },
	};
	Engine::Editor::DebugEditorMenuItem toolItems[] = {
		{ "Statistics", &debugStatsOpen_, "", "diagnostics.stats", "統計" },
		{ "Input Monitor", &debugKeyInputOpen_, "", "diagnostics.input", "キー操作デバッグ" },
		{ "Render Diagnostics", &debugOffscreenOpen_, "", "diagnostics.render", "オフスクリーン設定" },
		{ "Audio Monitor", &debugAudioOpen_, "", "diagnostics.audio", "オーディオ" },
		{ "Git", &gitWindowOpen_, "", "tool.git" },
		{ "MCP", &mcpWindowOpen_, "", "tool.mcp" },
		{ "Prefab Recovery", &prefabRecoveryWindowOpen_, "", "tool.prefab-recovery" },
		{ "Controller Recovery", &controllerRecoveryWindowOpen_, "", "tool.controller-recovery" },
		{ "Prefab Overrides", &prefabOverridesOpen_, "", "tool.prefab-overrides" },
		{ "Prefab Three-Way", &prefabThreeWayOpen_, "", "tool.prefab-three-way" },
	};

	Engine::Editor::DebugEditorMenuConfig menuConfig{};
	menuConfig.windowItems = windowItems;
	menuConfig.windowItemCount = std::size(windowItems);
	menuConfig.editItems = toolItems;
	menuConfig.editItemCount = std::size(toolItems);
	menuConfig.onApplyWorkspace = [this](bool diagnostics) {
		debugSceneViewOpen_ = debugGameViewOpen_ = genericHierarchyOpen_ = genericInspectorOpen_ = true;
		debugAssetBrowserOpen_ = debugConsoleOpen_ = true;
		debugStatsOpen_ = debugKeyInputOpen_ = debugOffscreenOpen_ = diagnostics;
		debugAudioOpen_ = false;
		imGuiManager->RequestDockWorkspace(diagnostics);
	};
	menuConfig.helpText = "KuboEngine Editor\nCtrl+K: Search editor commands\nCtrl+S: Save in scene tools\nCtrl+Z / Ctrl+Y: Undo / Redo in scene tools\nCtrl+D: Duplicate selection\nDelete: Remove selection in Hierarchy";
	menuConfig.onExport = [this]() { OpenExportWindow(); };
	menuConfig.onProjectSettings = [this]() { OpenProjectSettings(); };
	menuConfig.onOpenScene = [this]() { BrowseOpenScene(); };
	menuConfig.canOpenScene = CanOpenScene();
	const auto* busy = EditorDocumentBusyReason();
	menuConfig.openSceneUnavailableReason = busy ? busy : controllerEditor_.NeedsResolution() ? "Resolve the Controller draft first." :
		!genericSceneSelected_ || !genericWorldReady_ || projectFile_.empty() ? "Open a generic Scene project first." : "Check Project Settings and Prefab Recovery.";
	menuConfig.onNewScene = [this]() {std::string error;if(!RequestNewScene(error)) genericWorldStatus_=error;};
	menuConfig.onNewController=[this](){std::string error;if(!NewControllerAssetEditor(error))genericWorldStatus_=error;};
	menuConfig.canNewController = !busy && !controllerEditor_.NeedsResolution() && worldPlayback_.GetState() == WorldPlayback::State::Editing;
	menuConfig.newControllerUnavailableReason = busy ? busy : controllerEditor_.NeedsResolution() ?
		"Resolve the Controller draft first." : "Stop playback to create a Controller.";
	menuConfig.onSaveSceneAs = [this]() {BrowseSaveSceneAs();};
	menuConfig.canSaveSceneAs = CanSaveSceneAs();
	if (hubLaunched_) menuConfig.onReturnToHub = [this]() {
		RequestDocumentAction(Engine::Editor::EntityDocumentState::Action::Close);
	};
	ConfigureEditorMenuCommands(menuConfig);
	Engine::Editor::DebugEditorManager::DrawMainMenu(menuConfig);
	DrawExportWindow();
	DrawProjectSettingsWindow();
	DrawGitWindow();
	DrawMcpWindow();

	bool gizmoVisited = false;
	if (debugSceneViewOpen_) {
		const auto viewport = Engine::Editor::DebugEditorManager::DrawSceneViewport(&debugSceneViewOpen_,
			[this] {
				if (genericSceneSelected_) DrawEditorCameraToolbar();
			}, "Scene", 0, [this](Vector2 available) {
				if (genericSceneSelected_ && debugSceneViewOpen_ && offscreenRenderManager->HasEditorView()) {
					sceneResizeSchedule_.Observe(available.x,available.y,offscreenRenderManager->GetEditorViewExtent());
					if (const auto extent = sceneResizeSchedule_.Advance(frameDeltaTime_,offscreenRenderManager->GetEditorViewExtent())) {
						offscreenRenderManager->ResizeEditorView(*extent);
						sceneResizeSchedule_.Applied();
					}
				}
			}, [this,&gizmoVisited](const Engine::Editor::DebugSceneViewportState& viewport) {
				if (!genericSceneSelected_) return false;
				UpdateEditorViewCamera();
				DrawSceneGuides(viewport);
				const bool acceptingAsset = DrawSceneAssetDropTarget(viewport);
				if (acceptingAsset) return true;
				gizmoVisited = true; return DrawEntityGizmo(viewport);
			});
		if (!viewport.drawn || !debugSceneViewOpen_) sceneResizeSchedule_.Cancel();
		if (editorFocusPending_) { editorFocusPending_ = false; FocusEditorSelection(); }
		if (genericSceneSelected_ && editorViewEnabled_) {
			const auto& io = ImGui::GetIO();
			Engine::Editor::EditorCameraMotion motion;
			motion.navigate = ImGui::IsMouseDown(ImGuiMouseButton_Right);
			motion.forward = float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S));
			motion.right = float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A));
			motion.up = float(ImGui::IsKeyDown(ImGuiKey_E)) - float(ImGui::IsKeyDown(ImGuiKey_Q));
			motion.accelerate = io.KeyShift;
			motion.mouseX = io.MouseDelta.x; motion.mouseY = io.MouseDelta.y; motion.wheel = io.MouseWheel;
			const bool allowed = viewport.drawn && viewport.inputActive && !documentState_.HasPendingAction();
			editorCameraState_.Update(motion, frameDeltaTime_, allowed);
			if (allowed && !motion.navigate && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				editorPickX_ = 2 * (io.MousePos.x - viewport.min.x) / viewport.size.x - 1;
				editorPickY_ = 1 - 2 * (io.MousePos.y - viewport.min.y) / viewport.size.y;
				editorPickPending_ = true;
				editorPickAdditive_ = io.KeyCtrl;
			}
			if (allowed && ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusEditorSelection();
		}
	}
	else sceneResizeSchedule_.Cancel();
	if (!gizmoVisited && gizmoDragActive_) { entityHistory_.Commit(); gizmoDragActive_ = false;groupGizmoPivotValid_=false; }
	Engine::Editor::DebugEditorManager::DrawConsole(&debugConsoleOpen_);
	if (genericSceneSelected_ && debugGameViewOpen_) {
		const auto gameViewport = Engine::Editor::DebugEditorManager::DrawSceneViewport(&debugGameViewOpen_, [this] { const auto output=GetGameOutputExtent();ImGui::Text("Output: %u x %u",output.width,output.height);ImGui::TextUnformatted("Authored game camera; simulation follows Play/Pause. Hover image for script input."); },
			"Game", offscreenRenderManager->GetImGuiSceneTextureHandle().ptr);
		gameScriptInputAllowed_ = gameViewport.drawn && gameViewport.inputActive;
	}
	std::function<void(const std::filesystem::path&)> openAssetScene;
	if (CanOpenScene()) openAssetScene = [this](const std::filesystem::path& path) {
		std::string error;
		if (!RequestOpenScene(path,error)) genericWorldStatus_ = "Open rejected: " + error;
	};
	const char* assetRefreshBlockedReason = nullptr;
	if (prefabRecoveryBlocked_) assetRefreshBlockedReason = "Asset refresh paused. Resolve pending Prefab Recovery first.";
	else if (exportJob_.GetState() == Engine::Services::ExportJob::State::Running)
		assetRefreshBlockedReason = "Asset refresh paused during game export.";
	Engine::Editor::DebugEditorManager::DrawAssetBrowser(&debugAssetBrowserOpen_, projectAssetRoot_, &projectAssets_, &assetImportJob_,
		assetRefreshBlockedReason,
        worldPlayback_.GetState() == WorldPlayback::State::Editing && !documentState_.HasPendingAction(), GetProjectRoot(), projectFile_,
        [this](const std::string& guid){return CheckAssetMove(guid);}, &assetThumbnails_, std::move(openAssetScene));

	if (debugStatsOpen_) {
		ImGui::Begin("統計", &debugStatsOpen_);
		ImGui::Text("FPS: %.1f", frameDeltaTime_ > 0.0f ? 1.0f / frameDeltaTime_ : 0.0f);
		ImGui::Text("Frame Delta: %.4f sec", frameDeltaTime_);
		ImGui::SeparatorText("GPU frame timing");
		ImGui::Text("Average %.3f ms", dxCommon->GetAverageFrameGpuMilliseconds());
		ImGui::Text("Samples: %llu", static_cast<unsigned long long>(dxCommon->GetFrameGpuSampleCount()));
		if (ImGui::Button("Reset timing")) dxCommon->ResetGpuTimingStatistics();
		ImGui::End();
	}

	if (debugKeyInputOpen_) {
		ImGui::Begin("キー操作デバッグ", &debugKeyInputOpen_);
		Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
		if (ImGui::Button("Edit input bindings in Project Settings")) {
			OpenProjectSettings();
			openProjectInputSettings_ = true;
		}
		ImGui::Text("W: %s", input->PushKey(DIK_W) ? "down" : "up");
		ImGui::Text("A: %s", input->PushKey(DIK_A) ? "down" : "up");
		ImGui::Text("S: %s", input->PushKey(DIK_S) ? "down" : "up");
		ImGui::Text("D: %s", input->PushKey(DIK_D) ? "down" : "up");
		ImGui::Separator();
		ImGui::Text("Space: %s", input->PushKey(DIK_SPACE) ? "down" : "up");
		const Vector3 move = input->EvaluateAction("move");
		ImGui::Text("Action move: %.1f, %.1f, %.1f", move.x, move.y, move.z);
		ImGui::Text("Action start: %s", input->PushAction("start") ? "down" : "up");
		ImGui::Text("Left Mouse: %s", input->PushMouse(0) ? "down" : "up");
		ImGui::Text("Right Mouse: %s", input->PushMouse(1) ? "down" : "up");
		ImGui::End();
	}

	if (debugOffscreenOpen_) {
		if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
			offscreen->DrawImGui(&debugOffscreenOpen_);
		}
	}

	if (debugAudioOpen_) {
		ImGui::Begin("オーディオ", &debugAudioOpen_);
		if (ImGui::SliderFloat("Master Volume", &debugMasterVolume_, 0.0f, 1.0f)) {
			Engine::AudioSystem::Audio::GetInstance()->SetVolume(debugMasterVolume_);
		}
		if (ImGui::Button("Stop All")) {
			Engine::AudioSystem::Audio::GetInstance()->StopAudio();
		}
		ImGui::SameLine();
		if (ImGui::Button("Pause All")) {
			Engine::AudioSystem::Audio::GetInstance()->PauseAudio();
		}
		ImGui::SameLine();
		if (ImGui::Button("Resume All")) {
			Engine::AudioSystem::Audio::GetInstance()->ResumeAudio();
		}
		ImGui::End();
	}

	Engine::Editor::DebugEditorManager::SaveWindowItems(windowItems,std::size(windowItems));
	Engine::Editor::DebugEditorManager::SaveWindowItems(toolItems,std::size(toolItems));
#endif // _DEBUG
}

void Game::FocusEditorSelection()
{
	if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
		Engine::Editor::SceneBounds bounds;
		const float aspect = offscreenRenderManager->HasEditorView() ? offscreenRenderManager->GetEditorViewExtent().Aspect() : GetGameOutputExtent().Aspect();
		if (scene->GetWorldModelBounds(selectedEditorEntity_, bounds) && editorCameraState_.FrameBounds(bounds,0.45f,aspect)) return;
	}
	Matrix4x4 world{};
	if (editorWorld_.GetWorldMatrix(selectedEditorEntity_, world))
		editorCameraState_.Focus({world.m[3][0], world.m[3][1], world.m[3][2]});
}

void Game::DrawEditorCameraToolbar()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	ImGui::Checkbox("Editor view", &editorViewEnabled_);
	if (editorViewEnabled_) {
		const auto fitOnLine = [](float width) {
			const float end = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
			if (end - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
		};
		const auto fitButton = [&](const char* label) {
			fitOnLine(ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2);
		};
		fitButton("Camera");
		const bool openCamera = ImGui::Button("Camera");
#if defined(KUBO_APPLICATION_CHECKS)
		Engine::Editor::ObserveRenderInspectorChecks("Scene camera menu");
#endif
		if (openCamera) ImGui::OpenPopup("Scene Camera");
		if (ImGui::BeginPopup("Scene Camera")) {
			ImGui::Checkbox("Orthographic", &editorCameraState_.orthographic);
			const auto setView = [this](Vector3 rotation,bool orthographic) {
				editorCameraState_.rotation = rotation;
				editorCameraState_.orthographic = orthographic;
				if (selectedEditorEntity_) editorFocusPending_ = true;
				else editorCameraState_.Focus({0,0,0});
				ImGui::CloseCurrentPopup();
			};
			constexpr float quarterTurn = std::numbers::pi_v<float> / 2;
			if (ImGui::Button("Top")) setView({quarterTurn,0,0},true);
			ImGui::SameLine();
			if (ImGui::Button("Front")) setView({0,0,0},true);
			ImGui::SameLine();
			if (ImGui::Button("Right")) setView({0,-quarterTurn,0},true);
			ImGui::SameLine();
			if (ImGui::Button("Perspective")) setView({0.42f,-0.65f,0},false);
			if (ImGui::Button("2D (XY)")) {
				editorGridVisible_ = false;
				setView({0,0,0},true);
			}
			ImGui::SliderFloat("Fly speed", &editorCameraState_.speed,0.1f,100.0f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
			if (ImGui::Button("Reset camera")) editorCameraState_.Reset();
			ImGui::TextUnformatted("RMB + WASD/QE: fly   Shift: faster   Wheel: zoom");
			ImGui::EndPopup();
		}
		fitButton("Display");
		if (ImGui::Button("Display")) ImGui::OpenPopup("Scene Display");
		if (ImGui::BeginPopup("Scene Display")) {
			ImGui::Checkbox("XZ floor grid", &editorGridVisible_);
			ImGui::Checkbox("Transform gizmo", &gizmoEnabled_);
			ImGui::Checkbox("Selection bounds", &editorSelectionGuides_);
			ImGui::Checkbox("Camera, light and collider guides", &editorComponentGuides_);
			ImGui::EndPopup();
		}
		fitButton("Frame selection");
		ImGui::BeginDisabled(selectedEditorEntity_==0);
		if (ImGui::Button("Frame selection")) editorFocusPending_ = true;
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("F when the Scene image has focus");
		ImGui::BeginDisabled(gizmoDragActive_);
		ImGui::SetNextItemWidth(100);
		ImGui::Combo("##TransformOperation", &gizmoOperation_, "Move\0Rotate\0Scale\0");
		fitOnLine(85);
		ImGui::SetNextItemWidth(85);
		int axes = gizmoLocal_ ? 1 : 0;
		if (ImGui::Combo("##TransformAxes", &axes, "World\0Local\0")) gizmoLocal_ = axes == 1;
		const char* snapLabel = gizmoSnap_.enabled ? "Snap: On" : "Snap: Off";
		fitButton(snapLabel);
		if (ImGui::Button(snapLabel)) ImGui::OpenPopup("Scene Snap");
		if (ImGui::BeginPopup("Scene Snap")) {
			ImGui::Checkbox("Snap during drag", &gizmoSnap_.enabled);
			ImGui::SetNextItemWidth(100);
			if (gizmoOperation_==0) ImGui::InputFloat("Distance step", &gizmoSnap_.distance,0,0,"%.4f");
			else if (gizmoOperation_==1) ImGui::InputFloat("Angle step (degrees)", &gizmoSnap_.degrees,0,0,"%.4f");
			else ImGui::InputFloat("Scale ratio step", &gizmoSnap_.scaleRatio,0,0,"%.4f");
			ImGui::TextUnformatted("Snap is relative to the start of a drag.");
			if (gizmoOperation_==0) {
				const auto* unavailable = SceneEditUnavailableReason(Engine::Editor::SceneEditAction::Duplicate);
				ImGui::BeginDisabled(unavailable != nullptr);
				if (ImGui::Button("Align to world grid")) {
					std::string error;
					AlignEditorSelectionToWorldGrid(gizmoSnap_.distance,error);
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s",unavailable ? unavailable : "Align position to world-origin increments; preserve rotation and scale.");
			}
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
	} else ImGui::TextUnformatted("Authored camera preview in Scene; Game remains independent.");
#endif
}

bool Game::AlignEditorSelectionToWorldGrid(float step,std::string& error)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="World grid alignment requires an editing Scene and no pending document, gizmo, import or export";return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	const auto selection=editorSelection_.Capture();
	if (!Engine::Editor::AlignEntitiesToWorldGrid(editorWorld_,editorSelection_.Ids(),step,entityHistory_,selectedEditorEntity_,error,&selection)) {
		genericWorldStatus_="Grid alignment rejected: "+error;return false;
	}
	documentState_.Observe(editorWorld_);genericWorldStatus_="Aligned selection to world-origin grid";return true;
#else
	(void)step;error="World grid alignment requires the Editor";return false;
#endif
}

bool Game::SelectEditorEntity(EntityId id,bool additive,bool range,std::string& error)
{
	return SelectEditorEntityFromRows(id,additive,range,error,{});
}

bool Game::SelectEditorEntityFromRows(EntityId id,bool additive,bool range,std::string& error,std::span<const EntityId> rows)
{
	if(!editorMode_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsMovePending() || worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Entity selection requires an editing Scene without a pending document, drag or asset move";return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!editorSelection_.Click(editorWorld_,id,additive,range,error,rows))return false;
	entityHistory_.Commit();selectedEditorEntity_=editorSelection_.Primary();error.clear();return true;
}

void Game::UpdateEditorViewCamera()
{
	editorViewCamera_.SetTranslate(editorCameraState_.position);
	editorViewCamera_.SetRotate(editorCameraState_.rotation);
	editorViewCamera_.SetOrthographic(editorCameraState_.orthographic,editorCameraState_.height);
	editorViewCamera_.SetFarClip(editorCameraState_.farClip);
	if (offscreenRenderManager->HasEditorView())
		editorViewCamera_.SetAspectRatio(offscreenRenderManager->GetEditorViewExtent().Aspect());
	editorViewCamera_.Update();
}

void Game::DrawSceneGuides(const Engine::Editor::DebugSceneViewportState& viewport)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	using namespace Engine::Editor;
	if (!editorViewEnabled_ || (!editorSelectionGuides_ && !editorComponentGuides_)) return;
	if (worldPlayback_.GetState() != WorldPlayback::State::Editing || HasHierarchyHiddenSelection()) return;
	const auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene());
	const auto& viewProjection = editorViewCamera_.GetViewProjectionMatrix();
	const auto screen = [&](Vector2 ndc) {
		return ImVec2{viewport.min.x + (ndc.x + 1) * viewport.size.x * 0.5f,
			viewport.min.y + (1 - ndc.y) * viewport.size.y * 0.5f};
	};
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect({viewport.min.x,viewport.min.y},
		{viewport.min.x + viewport.size.x,viewport.min.y + viewport.size.y},true);
	const auto line = [&](Vector3 a, Vector3 b, ImU32 color) {
		Vector2 start{}, end{};
		if (ProjectSceneGuideLine(a,b,viewProjection,start,end)) {
			draw->AddLine(screen(start),screen(end),IM_COL32(0,0,0,190),3.0f);
			draw->AddLine(screen(start),screen(end),color,1.5f);
		}
	};
	const auto box = [&](const SceneBounds& bounds, ImU32 color) {
		const auto corner = [&](int i) {
			return Vector3{i & 1 ? bounds.max.x : bounds.min.x,
				i & 2 ? bounds.max.y : bounds.min.y,i & 4 ? bounds.max.z : bounds.min.z};
		};
		for (int i = 0; i < 8; ++i) {
			for (const int axis : {1,2,4}) {
				if (!(i & axis)) line(corner(i),corner(i | axis),color);
			}
		}
	};
	for (const EntityId id : GetEditorSelection()) {
		const auto* entity = editorWorld_.Find(id);
		Matrix4x4 world{};
		if (!entity || !editorWorld_.GetWorldMatrix(id,world)) continue;
		const Vector3 center{world.m[3][0],world.m[3][1],world.m[3][2]};
		SceneBounds bounds{};
		constexpr ImU32 selectionColor = IM_COL32(110,190,255,255);
		if (editorSelectionGuides_ && scene && scene->GetWorldModelBounds(id,bounds))
			box(bounds,selectionColor);
		if (editorComponentGuides_ && entity->hasCollider) {
			const auto color = entity->collider.isTrigger ? IM_COL32(207,159,255,255) : IM_COL32(115,234,162,255);
			if (entity->collider.shape==Physics::ColliderShape::Box) {
				const auto half = entity->collider.halfExtents;
				if (TransformSceneBounds({half * -1,half},world,bounds)) box(bounds,color);
			} else {
				using namespace Physics::Geometry;
				const auto round=TransformRounded(entity->collider,world);
				const auto middle=ToPoint(center);
				const double length=std::sqrt(Dot(round.halfAxis,round.halfAxis));
				const auto axis=length>0 ? round.halfAxis*(1/length) : Point{{0,1,0}};
				const auto tangent=Cross(axis,std::abs(axis[0])<0.8 ? Point{{1,0,0}} : Point{{0,1,0}});
				const auto u=tangent*(1/std::sqrt(Dot(tangent,tangent))),v=Cross(axis,u);
				const auto arc=[&](Point origin,Point a,Point b,double angle,double span) {
					Point previous=origin+(a*std::cos(angle)+b*std::sin(angle))*round.radius;
					for(int i=1;i<=24;++i) {
						const double t=angle+span*i/24;
						const auto next=origin+(a*std::cos(t)+b*std::sin(t))*round.radius;
						line(ToVector(previous),ToVector(next),color);
						previous=next;
					}
				};
				constexpr double pi=3.141592653589793;
				arc(middle+round.halfAxis,u,v,0,2*pi);
				if (length>0) arc(middle-round.halfAxis,u,v,0,2*pi);
				for (const auto tangentAxis : {u,v}) {
					arc(middle+round.halfAxis,tangentAxis,axis,0,pi);
					arc(middle-round.halfAxis,tangentAxis,axis,pi,pi);
					if (length==0) continue;
					for (const double sign : {-1.0,1.0}) {
						const auto side=middle+tangentAxis*(sign*round.radius);
						line(ToVector(side-round.halfAxis),ToVector(side+round.halfAxis),color);
					}
				}
			}
		}
		Vector2 marker{}, unused{};
		if (!ProjectSceneGuideLine(center,center,viewProjection,marker,unused)) continue;
		const auto position = screen(marker);
		if (editorSelectionGuides_) draw->AddCircle(position,4,selectionColor,0,1.5f);
		if (editorComponentGuides_ && entity->hasCamera)
			draw->AddText({position.x+8,position.y-ImGui::GetFontSize()},IM_COL32(142,221,255,255),ICON_FA_VIDEO);
		if (editorComponentGuides_ && entity->hasLight)
			draw->AddText({position.x+8,position.y},IM_COL32(255,217,112,255),ICON_FA_LIGHTBULB);
	}
	draw->PopClipRect();
#else
	(void)viewport;
#endif
}

#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
static std::string GetAssetPayloadGuid(const ImGuiPayload* payload)
{
	if (!payload || !payload->IsDataType(Engine::Editor::AssetGuidPayload) ||
		(payload->DataSize != 16 && payload->DataSize != 32)) return {};
	return {static_cast<const char*>(payload->Data),static_cast<size_t>(payload->DataSize)};
}
#endif

bool Game::DrawSceneAssetDropTarget(const Engine::Editor::DebugSceneViewportState& viewport)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	const auto guid = GetAssetPayloadGuid(ImGui::GetDragDropPayload());
	if (guid.empty() || !editorViewEnabled_ ||
		worldPlayback_.GetState() != WorldPlayback::State::Editing || EditorDocumentBusyReason()) return false;
	const auto* active = projectAssets_.FindByGuid(guid);
	if (!active || (active->type != Assets::AssetType::Model && active->type != Assets::AssetType::Prefab)) return false;
	const ImRect image({viewport.min.x,viewport.min.y},
		{viewport.min.x+viewport.size.x,viewport.min.y+viewport.size.y});
	if (!ImGui::BeginDragDropTargetCustom(image,ImGui::GetID("Scene asset drop"))) return false;
	const auto* payload = ImGui::AcceptDragDropPayload(Engine::Editor::AssetGuidPayload);
	const bool deliver = payload && payload->IsDelivery();
	ImGui::EndDragDropTarget();
	if (!deliver) return true;
	const auto mouse = ImGui::GetMousePos();
	const float x = 2 * (mouse.x-viewport.min.x)/viewport.size.x - 1;
	const float y = 1 - 2 * (mouse.y-viewport.min.y)/viewport.size.y;
	Engine::Editor::ScenePickSegment ray{};
	std::string error;
	if (Engine::Editor::MakeScenePickSegment(x,y,editorViewCamera_.GetViewProjectionMatrix(),ray)) {
		const auto direction = ray.farPoint - ray.nearPoint;
		// Perspective placement favors the floor. Orthographic placement
		// favors the world plane facing the view.
		const float begin[] = {ray.nearPoint.y,ray.nearPoint.z,ray.nearPoint.x};
		const float delta[] = {direction.y,direction.z,direction.x};
		const auto strongest = std::max_element(std::begin(delta),std::end(delta),
			[](float a,float b) {return std::abs(a) < std::abs(b);});
		const size_t firstPlane = editorCameraState_.orthographic ? size_t(strongest-std::begin(delta)) : 0;
		for (size_t offset = 0; offset < std::size(begin); ++offset) {
			const size_t i = (firstPlane + offset) % std::size(begin);
			if (std::abs(delta[i]) < 1e-5f) continue;
			const float fraction = -begin[i]/delta[i];
			if (fraction < 0 || fraction > 1) continue;
			PlaceSceneAsset(guid,ray.nearPoint + direction * fraction,error);
			if (!error.empty()) genericWorldStatus_ = "Asset placement rejected: " + error;
			return true;
		}
	}
	genericWorldStatus_ = "Asset placement needs a visible world-origin plane under the cursor";
	return true;
#else
	(void)viewport; return false;
#endif
}

bool Game::DrawEntityGizmo(const Engine::Editor::DebugSceneViewportState& viewport)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	using Gizmo = Engine::Editor::ImGuizmoManager;
	const auto finish = [&] { if (gizmoDragActive_) entityHistory_.Commit(); gizmoDragActive_ = false;groupGizmoPivotValid_=false; };
	auto* entity = editorWorld_.Find(selectedEditorEntity_);
	const bool group=GetEditorSelection().size()>1;
	std::string recoveryGuard;
	const bool allowed = CheckPrefabRecovery(recoveryGuard) && !HasHierarchyHiddenSelection() && genericSceneSelected_ && editorViewEnabled_ && gizmoEnabled_ && entity && viewport.drawn &&
		worldPlayback_.GetState() == WorldPlayback::State::Editing && !documentState_.HasPendingAction() &&
		!assetImportJob_.IsBusy() && exportJob_.GetState()!=Engine::Services::ExportJob::State::Running && !ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (!allowed) { Gizmo::Enable(false); finish(); return false; }
	Matrix4x4 world{},parent{}; for (int i=0;i<4;++i) parent.m[i][i]=1;
	std::string error;
	if(group) {
		if(groupGizmoPivotValid_)world=groupGizmoMatrix_;
		else if(!Engine::Editor::BuildGroupPivot(editorWorld_,editorSelection_,gizmoLocal_,world,error)) {
			genericWorldStatus_="Group gizmo unavailable: "+error;Gizmo::Enable(false);finish();return false;
		}
	} else if (!editorWorld_.GetWorldMatrix(entity->id,world) || (entity->parent && !editorWorld_.GetWorldMatrix(entity->parent,parent))) {
		Gizmo::Enable(false); finish(); return false;
	}
	std::array<float,3> snap{};
	if (!gizmoSnap_.Resolve(gizmoOperation_,snap,error)) {
		genericWorldStatus_ = "Gizmo unavailable: " + error; Gizmo::Enable(false); finish(); return false;
	}
	Engine::Scene::TransformComponent checked;
	if (!group && !Engine::Editor::GizmoWorldToLocal(world,parent,entity->transform,checked,error)) {
		genericWorldStatus_ = "Gizmo unavailable: " + error; Gizmo::Enable(false); finish(); return false;
	}
	Gizmo::Enable(viewport.inputActive || gizmoDragActive_);
	Gizmo::SetOrthographic(editorCameraState_.orthographic); Gizmo::SetDrawlist();
	Gizmo::SetRect(viewport.min.x,viewport.min.y,viewport.size.x,viewport.size.y);
	ImGui::GetWindowDrawList()->PushClipRect({viewport.min.x,viewport.min.y},{viewport.min.x+viewport.size.x,viewport.min.y+viewport.size.y},true);
	const auto fromPivot=world;
	const bool changed = Gizmo::Manipulate(&editorViewCamera_.GetViewMatrix().m[0][0],&editorViewCamera_.GetProjectionMatrix().m[0][0],&world.m[0][0],
		static_cast<Gizmo::Operation>(gizmoOperation_),gizmoLocal_ ? Gizmo::Mode::Local : Gizmo::Mode::World,
		gizmoSnap_.enabled ? snap.data() : nullptr);
	ImGui::GetWindowDrawList()->PopClipRect();
	const bool usingGizmo = Gizmo::IsUsing();
	if (!gizmoDragActive_ && (usingGizmo || changed)) entityHistory_.Commit();
	if (changed) {
		if(group) {
			if(Engine::Editor::ApplyGroupTransform(editorWorld_,editorSelection_,fromPivot,world,gizmoOperation_,entityHistory_,error))documentState_.Observe(editorWorld_);
			else world=fromPivot;
		} else {
		Engine::Scene::TransformComponent candidate;
		if (Engine::Editor::GizmoWorldToLocal(world,parent,entity->transform,candidate,error)) {
			const auto before=editorWorld_.Capture(); const auto old=entity->transform;
			entity->transform=candidate;
			if (!entityHistory_.Record(before,editorWorld_,editorSelection_.Capture(),editorSelection_.Capture(),error)) entity->transform=old;
			else documentState_.Observe(editorWorld_);
		}
		}
		if (!error.empty()) genericWorldStatus_ = "Gizmo edit rejected: " + error;
	}
	gizmoDragActive_ = usingGizmo;
	groupGizmoPivotValid_=group && usingGizmo;if(groupGizmoPivotValid_)groupGizmoMatrix_=world;
	if (!usingGizmo) entityHistory_.Commit();
	return usingGizmo || Gizmo::IsOver();
#else
	(void)viewport; return false;
#endif
}

void Game::DrawMcpWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (!mcpWindowOpen_) return;
	if (ImGui::Begin("MCP", &mcpWindowOpen_)) {
		ImGui::TextWrapped("Opt-in local automation of this Editor's generic entity scene. Same-user local clients can list, create and save entities while enabled. Disable when not in use.");
		if (mcpSession_.IsRunning()) {
			ImGui::TextUnformatted("MCP enabled for the opened project");
			if (ImGui::Button("Disable MCP")) { mcpSession_.Stop(); mcpStatus_ = "MCP disabled"; }
		} else {
			ImGui::BeginDisabled(projectFile_.empty() || !genericSceneSelected_ || worldPlayback_.GetState() != WorldPlayback::State::Editing);
			if (ImGui::Button("Enable local MCP automation")) {
				if (mcpSession_.Start(projectFile_.parent_path(), mcpStatus_)) mcpStatus_ = "MCP enabled";
			}
			ImGui::EndDisabled();
		}
		if (!genericSceneSelected_) ImGui::TextUnformatted("Available only for manifest-selected generic EntityWorld scenes.");
		ImGui::TextWrapped("External adapter: KuboMcpServer.exe --project <absolute .kubo path>");
		ImGui::TextWrapped("%s", mcpStatus_.c_str());
	}
	ImGui::End();
#endif
}

bool Game::StartGitIndexEdit(const Engine::Services::GitStatus& expected,const Engine::Services::GitChange& selected,bool stage,std::string& error)
{
	if(!editorMode_ || projectFile_.empty() || prefabRecoveryBlocked_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing ||
		documentState_.IsDirty() || documentState_.HasPendingAction() || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid()) {
		error="Git index editing requires a saved, idle project in Editing mode; wait for jobs and refresh status";return false;
	}
	try {
		const auto root=projectFile_.parent_path();
		gitIndexRequest_=std::async(std::launch::async,[root,expected,selected,stage]{
			GitIndexResult result;result.success=Engine::Services::GitWorkspace::UpdateIndex(root,expected,selected,stage,result.message);
			if(result.success)result.message=stage ? "Selected disk content staged; working files retained." : "Selected entry unstaged; working files retained.";
			result.status=Engine::Services::GitWorkspace::ReadStatus(root);return result;
		});
		gitDiff_.display.clear();gitDiff_.error="Git index changed or is changing; request a fresh diff.";
		error.clear();return true;
	}catch(const std::exception& failure){error=failure.what();return false;}
}

bool Game::StartGitCommit(const Engine::Services::GitStatus& expected,const std::string& message,std::string& error)
{
	if(!editorMode_ || projectFile_.empty() || prefabRecoveryBlocked_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing ||
		documentState_.IsDirty() || documentState_.HasPendingAction() || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid()) {
		error="Git commit requires a saved, idle project in Editing mode; wait for jobs and refresh status";return false;
	}
	try {
		const auto root=projectFile_.parent_path();
		gitIndexRequest_=std::async(std::launch::async,[root,expected,message]{
			GitIndexResult result;result.success=Engine::Services::GitWorkspace::Commit(root,expected,message,result.message);
			if(result.success)result.message="Staged project changes committed; unstaged files retained.";
			result.status=Engine::Services::GitWorkspace::ReadStatus(root);return result;
		});
		gitDiff_.display.clear();gitDiff_.error="Git commit is changing HEAD/index; request a fresh diff.";
		gitHistory_.history={};gitHistory_.error="Commit is changing HEAD; request fresh project history.";
		error.clear();return true;
	}catch(const std::exception& failure){error=failure.what();return false;}
}

bool Game::StartGitDiff(const Engine::Services::GitStatus& expected,const Engine::Services::GitChange& selected,bool staged,std::string& error)
{
	if(!editorMode_ || projectFile_.empty() || prefabRecoveryBlocked_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() ||
		exportJob_.GetState()==Engine::Services::ExportJob::State::Running || gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_) {
		error="Wait for project/Git jobs and confirmations before reading a diff";return false;
	}
	try {
		const auto root=projectFile_.parent_path();
		gitDiffRequest_=std::async(std::launch::async,[root,expected,selected,staged]{
			GitDiffResult result;result.label=(staged ? "Staged (HEAD / index): " : "Unstaged (index / disk): ")+selected.path;
			Engine::Services::GitWorkspace::ReadDiff(root,expected,selected,staged,result.display,result.error);return result;
		});
		gitDiff_={};gitDiffOpen_=true;error.clear();return true;
	}catch(const std::exception& failure){error=failure.what();return false;}
}

bool Game::StartGitHistory(const Engine::Services::GitStatus& expected,size_t skip,std::string& error)
{
	if(!editorMode_ || projectFile_.empty() || prefabRecoveryBlocked_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() ||
		exportJob_.GetState()==Engine::Services::ExportJob::State::Running || gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_) {
		error="Wait for project/Git jobs and confirmations before reading history";return false;
	}
	try {
		const auto root=projectFile_.parent_path();auto snapshot=expected;
		gitHistoryRequest_=std::async(std::launch::async,[root,expected,skip]{GitHistoryResult result;Engine::Services::GitWorkspace::ReadHistory(root,expected,skip,result.history,result.error);return result;});
		gitHistoryExpected_=std::move(snapshot);gitHistory_={};gitHistoryOpen_=true;error.clear();return true;
	}catch(const std::exception& failure){error=failure.what();return false;}
}

void Game::DrawGitWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (gitRequest_.valid() && gitRequest_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try { gitStatus_ = gitRequest_.get(); }
		catch (const std::exception& e) { gitStatus_ = {}; gitStatus_.error = e.what(); }
		gitHasResult_ = true;
	}
	if(gitIndexRequest_.valid() && gitIndexRequest_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
		try {auto result=gitIndexRequest_.get();gitIndexMessage_=std::move(result.message);gitStatus_=std::move(result.status);}
		catch(const std::exception& failure){gitIndexMessage_=failure.what();gitStatus_={};gitStatus_.error="Refresh Git status after the failed operation";}
		gitHasResult_=true;
	}
	if(gitDiffRequest_.valid() && gitDiffRequest_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
		try {gitDiff_=gitDiffRequest_.get();}catch(const std::exception& failure){gitDiff_={};gitDiff_.error=failure.what();}
	}
	if(gitHistoryRequest_.valid() && gitHistoryRequest_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
		try {gitHistory_=gitHistoryRequest_.get();}catch(const std::exception& failure){gitHistory_={};gitHistory_.error=failure.what();}
	}
	if(gitHistoryOpen_) {
		ImGui::SetNextWindowSize({900,470},ImGuiCond_FirstUseEver);
		if(ImGui::Begin("Git project history",&gitHistoryOpen_)) {
			ImGui::TextWrapped("Read-only path-limited history of the current project folder, not all repository commits or rename-following history. Dates include author timezone. Reopen after external HEAD changes. No checkout/reset/revert.");
			if(gitHistoryRequest_.valid())ImGui::TextUnformatted("Reading history...");
			if(!gitHistory_.error.empty())ImGui::TextWrapped("%s",gitHistory_.error.c_str());
			else if(!gitHistoryRequest_.valid()) {
				const auto& history=gitHistory_.history;
				ImGui::TextWrapped("HEAD snapshot: %s",history.head.empty() ? "(unborn branch)" : history.head.c_str());
				ImGui::Text("Offset: %zu / Entries: %zu",history.skip,history.entries.size());
				if(history.entries.empty())ImGui::TextUnformatted("No project commits on this page.");
				const bool busy=gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_;
				ImGui::BeginDisabled(busy || history.skip==0);if(ImGui::Button("Newer page")){const auto skip=history.skip>=50 ? history.skip-50 : 0;std::string error;if(!StartGitHistory(gitHistoryExpected_,skip,error))gitIndexMessage_=std::move(error);}ImGui::EndDisabled();ImGui::SameLine();
				ImGui::BeginDisabled(busy || gitHistoryRequest_.valid() || !gitHistory_.history.more || gitHistory_.history.skip>=10000);if(ImGui::Button("Older page")){const auto skip=gitHistory_.history.skip+50;std::string error;if(!StartGitHistory(gitHistoryExpected_,skip,error))gitIndexMessage_=std::move(error);}ImGui::EndDisabled();
				if(gitHistory_.history.skip>=10000 && gitHistory_.history.more)ImGui::TextUnformatted("History offset limit reached (10000). Use external Git for older records.");
				if(ImGui::BeginTable("Project commits",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,{0,0})) {
					ImGui::TableSetupColumn("Commit",ImGuiTableColumnFlags_WidthFixed,100);ImGui::TableSetupColumn("Author",ImGuiTableColumnFlags_WidthFixed,150);ImGui::TableSetupColumn("Author date",ImGuiTableColumnFlags_WidthFixed,215);ImGui::TableSetupColumn("Subject");ImGui::TableHeadersRow();
					for(const auto& entry:gitHistory_.history.entries){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(entry.hash.substr(0,12).c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",entry.hash.c_str());ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(entry.author.c_str());ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(entry.date.c_str());ImGui::TableSetColumnIndex(3);ImGui::TextUnformatted(entry.subject.c_str());}
					ImGui::EndTable();
				}
			}
		}ImGui::End();
	}
	if(gitDiffOpen_) {
		ImGui::SetNextWindowSize({760,500},ImGuiCond_FirstUseEver);
		if(ImGui::Begin("Git diff",&gitDiffOpen_)) {
			ImGui::TextUnformatted(gitDiff_.label.c_str());
			ImGui::TextWrapped("Read-only observed disk/index content, not unsaved Scene buffers or a live snapshot. Reopen after external edits. Binary files show summaries; control/non-UTF8 bytes are escaped.");
			if(gitDiffRequest_.valid())ImGui::TextUnformatted("Reading diff...");
			if(!gitDiff_.error.empty())ImGui::TextWrapped("%s",gitDiff_.error.c_str());
			else if(!gitDiffRequest_.valid() && gitDiff_.display.empty())ImGui::TextUnformatted("No textual difference returned.");
			if(ImGui::BeginChild("Diff content",{0,0},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar))ImGui::TextUnformatted(gitDiff_.display.c_str());
			ImGui::EndChild();
		}ImGui::End();
	}
	if (!gitWindowOpen_) return;
	ImGui::SetNextWindowSize({620.0f, 420.0f}, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Git", &gitWindowOpen_)) {
		ImGui::TextWrapped("Stage / Unstage edits the index; Commit records existing staged project changes. Save the Scene first. No worktree restore, pull or push.");
		const bool reading = gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid();
		ImGui::BeginDisabled(reading || projectFile_.empty() || gitIndexSelection_.has_value() || gitCommitSelection_.has_value());
		const bool refresh = ImGui::Button("Refresh Git status");
		ImGui::EndDisabled();
		if (!reading && !gitIndexSelection_ && !gitCommitSelection_ && !projectFile_.empty() && (refresh || !gitHasResult_)) {
			gitDiff_.display.clear();gitDiff_.error="Status refreshed; request a fresh diff.";
			gitHistory_.history={};gitHistory_.error="Status refreshed; request fresh project history.";
			const auto root = projectFile_.parent_path();
			try { gitRequest_ = std::async(std::launch::async, [root]() { return Engine::Services::GitWorkspace::ReadStatus(root); }); }
			catch (const std::exception& e) { gitStatus_.error = e.what(); gitHasResult_ = true; }
		}
		if (projectFile_.empty()) ImGui::TextUnformatted("Open a .kubo project first.");
		if (gitRequest_.valid()) ImGui::TextUnformatted("Reading Git status...");
		if(gitIndexRequest_.valid())ImGui::TextUnformatted("Running Git operation...");
		if(!gitIndexMessage_.empty())ImGui::TextWrapped("%s",gitIndexMessage_.c_str());
		if (!gitStatus_.error.empty()) ImGui::TextWrapped("%s", gitStatus_.error.c_str());
		else if (gitHasResult_ && gitStatus_.repository) {
			ImGui::TextWrapped("Repository: %s", gitStatus_.repositoryRoot.c_str());
			ImGui::Text("Branch: %s", gitStatus_.detached ? "(detached HEAD)" : gitStatus_.branch.c_str());
			ImGui::Text("Changed entries in project: %zu", gitStatus_.changes.size());
			const bool historyLocked=reading || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_ || prefabRecoveryBlocked_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running;
			ImGui::BeginDisabled(historyLocked);if(ImGui::Button("Read project history")){std::string error;if(!StartGitHistory(gitStatus_,0,error))gitIndexMessage_=std::move(error);}ImGui::EndDisabled();
			const bool commitLocked=gitRequest_.valid() || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_ || prefabRecoveryBlocked_ || documentState_.IsDirty() || documentState_.HasPendingAction() ||
				worldPlayback_.GetState()!=WorldPlayback::State::Editing || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running;
			ImGui::BeginDisabled(commitLocked);
			ImGui::InputTextMultiline("Commit message",gitCommitDraft_,sizeof(gitCommitDraft_),{0,65});
			ImGui::BeginDisabled(gitStatus_.detached || gitStatus_.indexRevision.empty() || !gitCommitDraft_[0]);
			if(ImGui::Button("Commit staged project changes...")) {
				try {gitCommitSelection_=GitCommitSelection{gitStatus_,gitCommitDraft_};ImGui::OpenPopup("Confirm Git commit");}
				catch(const std::exception& failure){gitIndexMessage_=failure.what();}
			}
			ImGui::EndDisabled();ImGui::EndDisabled();
			if(ImGui::BeginPopupModal("Confirm Git commit",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
				if(gitCommitSelection_) {
					const auto& selection=*gitCommitSelection_;
					ImGui::TextWrapped("Branch: %s",selection.expected.branch.c_str());
					ImGui::TextUnformatted(selection.message.c_str());
					ImGui::TextWrapped("Commit ALL existing staged changes, only if every path belongs to this project. Unstaged disk changes are not added. Outside-project entries or stale HEAD/index refuse the request. Trusted hooks/signing run; hooks and concurrent external Git operations are not sandboxed.");
					if(ImGui::Button("Confirm commit")){std::string error;if(!StartGitCommit(selection.expected,selection.message,error))gitIndexMessage_=std::move(error);gitCommitSelection_.reset();ImGui::CloseCurrentPopup();}
					ImGui::SameLine();if(ImGui::Button("Cancel commit")){gitCommitSelection_.reset();ImGui::CloseCurrentPopup();}
				}else ImGui::CloseCurrentPopup();ImGui::EndPopup();
			}
			if (gitStatus_.changes.empty()) ImGui::TextUnformatted("No changes in the selected project.");
			if (ImGui::BeginTable("Git changes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 0})) {
				ImGui::TableSetupColumn("Index / Worktree", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Path (repository relative)");
				ImGui::TableSetupColumn("Previous path");
				ImGui::TableSetupColumn("Diff / index action",ImGuiTableColumnFlags_WidthFixed,220.0f);
				ImGui::TableHeadersRow();
				int gitRow=0;
				for (const auto& change : gitStatus_.changes) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%c / %c", change.index, change.worktree);
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(change.path.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(change.originalPath.c_str());
					ImGui::TableSetColumnIndex(3);ImGui::PushID(gitRow++);
					const bool diffLocked=reading || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexRequest_.valid() || gitIndexSelection_ || gitCommitSelection_ || prefabRecoveryBlocked_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running;
					ImGui::BeginDisabled(diffLocked || gitHistoryRequest_.valid() || change.worktree==' ');if(ImGui::SmallButton("Unstaged diff")){std::string error;if(!StartGitDiff(gitStatus_,change,false,error))gitIndexMessage_=std::move(error);}ImGui::EndDisabled();ImGui::SameLine();
					ImGui::BeginDisabled(diffLocked || gitDiffRequest_.valid() || change.index==' ' || change.index=='?' || change.index=='!');if(ImGui::SmallButton("Staged diff")){std::string error;if(!StartGitDiff(gitStatus_,change,true,error))gitIndexMessage_=std::move(error);}ImGui::EndDisabled();
					const bool locked=reading || gitIndexRequest_.valid() || gitDiffRequest_.valid() || gitHistoryRequest_.valid() || gitIndexSelection_ || gitCommitSelection_ || prefabRecoveryBlocked_ || documentState_.IsDirty() || documentState_.HasPendingAction() ||
						worldPlayback_.GetState()!=WorldPlayback::State::Editing || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running;
					const auto select=[&](bool stage){try{gitIndexSelection_=GitIndexSelection{gitStatus_,change,stage};ImGui::OpenPopup("Confirm Git index edit");}catch(const std::exception& failure){gitIndexMessage_=failure.what();}};
					ImGui::BeginDisabled(locked || change.worktree==' ');if(ImGui::SmallButton("Stage"))select(true);ImGui::EndDisabled();ImGui::SameLine();
					ImGui::BeginDisabled(locked || change.index==' ' || change.index=='?' || change.index=='!');if(ImGui::SmallButton("Unstage"))select(false);ImGui::EndDisabled();
					if(ImGui::BeginPopupModal("Confirm Git index edit",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
						if(gitIndexSelection_){const auto& selection=*gitIndexSelection_;ImGui::TextWrapped("%s: %s",selection.stage ? "Stage current disk contents" : "Unstage index entry",selection.selected.path.c_str());
							if(!selection.selected.originalPath.empty())ImGui::TextWrapped("Previous path: %s",selection.selected.originalPath.c_str());
							ImGui::TextWrapped("Directories include their matching descendants. Working files are retained. Repository filters/configuration must be trusted. Changed status/HEAD/staged content refuses this request.");
							if(ImGui::Button("Confirm index edit")){std::string error;if(!StartGitIndexEdit(selection.expected,selection.selected,selection.stage,error))gitIndexMessage_=std::move(error);gitIndexSelection_.reset();ImGui::CloseCurrentPopup();}
							ImGui::SameLine();if(ImGui::Button("Cancel index edit")){gitIndexSelection_.reset();ImGui::CloseCurrentPopup();}
						}else ImGui::CloseCurrentPopup();ImGui::EndPopup();
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
	}
	ImGui::End();
#endif
}

void Game::ReloadProjectSettings()
{
	std::string error;
	// Do not leave a previous snapshot saveable after a failed reload.
	projectSettingsContext_ = {};
	if(projectFile_.empty() || !projectSettingsContext_.Open(projectFile_,error)) {
		projectSettingsStatus_ = projectFile_.empty() ? "Open a .kubo project first." : error;
		return;
	}
	const auto extent=projectSettingsContext_.GetSettings().gameOutput;
	projectOutputDraft_[0]=static_cast<int>(extent.width);
	projectOutputDraft_[1]=static_cast<int>(extent.height);
	const auto limits=projectSettingsContext_.GetSettings().luaLimits;
	projectLuaDraft_[0]=static_cast<int>(limits.memoryBytes);projectLuaDraft_[1]=static_cast<int>(limits.states);projectLuaDraft_[2]=static_cast<int>(limits.frameInstructions);
	const auto& startup=projectSettingsContext_.GetSettings().startupScene;
	std::memcpy(projectStartupDraft_,startup.c_str(),startup.size()+1);
	projectSettingsStatus_="Settings loaded. Saved changes apply on the next Editor/Player launch.";
}

void Game::SelectStartupScene(const std::filesystem::path& path)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	std::string relative,error;
	if(!Engine::Editor::SelectProjectStartupScene(projectSettingsContext_,path,relative,error)) {projectSettingsStatus_=error;return;}
	if(relative.size()>=sizeof(projectStartupDraft_)) {projectSettingsStatus_="Startup Scene path exceeds 4096 UTF-8 bytes";return;}
	std::memcpy(projectStartupDraft_,relative.c_str(),relative.size()+1);
	projectSettingsStatus_="Scene selected. Save for next launch to update the project.";
#else
	(void)path;
#endif
}

void Game::DrawProjectInputSettings()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	auto* input = Engine::InputSystem::Input::GetInstance();
	const auto& bindingPath = input->GetActionBindingsPath();
	const bool bindingEditAllowed = !bindingPath.empty() && worldPlayback_.GetState() == WorldPlayback::State::Editing &&
		!prefabRecoveryBlocked_ && !documentState_.HasPendingAction() && !controllerClosePending_ && !gizmoDragActive_ &&
		exportJob_.GetState() != Engine::Services::ExportJob::State::Running && !assetImportJob_.IsBusy();
	if (!bindingEditAllowed) ImGui::TextWrapped("Input bindings can be changed in an idle Editor with a project settings path.");
	static std::string bindingStatus;
	static char bindingAction[257]="jump";
	static int bindingSource=0,bindingCode=0;
	static float bindingContribution[3]={1,0,0},bindingThreshold=0.5f;
	ImGui::TextWrapped("%s",Engine::Base::PathToUtf8(bindingPath).c_str());
	ImGui::InputText("Action",bindingAction,sizeof(bindingAction));
	if(ImGui::Combo("Source",&bindingSource,"Keyboard scan code\0Pad button\0Pad axis\0"))bindingCode=0;
	if(bindingSource==0)ImGui::SliderInt("Scan code",&bindingCode,0,255);
	else if(bindingSource==1)ImGui::Combo("Button",&bindingCode,"DpadUp\0DpadDown\0DpadLeft\0DpadRight\0Start\0Back\0LeftStick\0RightStick\0LeftShoulder\0RightShoulder\0A\0B\0X\0Y\0");
	else {ImGui::Combo("Axis",&bindingCode,"LeftX\0LeftY\0RightX\0RightY\0LeftTrigger\0RightTrigger\0");ImGui::SliderFloat("Activation threshold",&bindingThreshold,0.01f,1.0f);}
	ImGui::InputFloat3("Contribution",bindingContribution);

	ImGui::BeginDisabled(!bindingEditAllowed || bindingAction[0]=='\0');
	if(ImGui::Button("Add / replace binding")) {
		const Vector3 contribution{bindingContribution[0],bindingContribution[1],bindingContribution[2]};
		bool replacing=false;
		if(const auto existing=input->GetActionMap().Bindings().find(bindingAction);existing!=input->GetActionMap().Bindings().end())for(const auto& binding:existing->second)if(static_cast<int>(binding.source)==bindingSource && binding.key==bindingCode)replacing=true;
		if(bindingCode<0 || bindingCode>(bindingSource==0 ? 255 : bindingSource==1 ? 13 : 5))bindingStatus="Binding code out of range";
		else if(bindingSource==2 && (!std::isfinite(bindingThreshold) || bindingThreshold<=0 || bindingThreshold>1))bindingStatus="Threshold must be in (0,1]";
		else if(!std::isfinite(contribution.x) || !std::isfinite(contribution.y) || !std::isfinite(contribution.z))bindingStatus="Contribution must be finite";
		else if(input->GetActionMap().Bindings().size()>=256 && !input->GetActionMap().HasAction(bindingAction))bindingStatus="Too many actions";
		else if(!replacing && input->GetActionMap().BindingCount(bindingAction)>=256)bindingStatus="Too many bindings";
		else {
			if(bindingSource==0)input->BindKeyAction(bindingAction,static_cast<BYTE>(bindingCode),contribution);
			else if(bindingSource==1)input->BindPadButtonAction(bindingAction,static_cast<Engine::InputSystem::GamepadButton>(bindingCode),contribution);
			else input->BindPadAxisAction(bindingAction,static_cast<Engine::InputSystem::ActionMap::PadAxis>(bindingCode),contribution,bindingThreshold);
			bindingStatus="Binding changed in memory; Save bindings to persist";
		}
	}
	ImGui::SameLine();if(ImGui::Button("Clear action bindings")){input->ClearAction(bindingAction);bindingStatus="Action cleared in memory; Save bindings to persist";}
	ImGui::EndDisabled();
	ImGui::Text("Selected action bindings: %zu",input->GetActionMap().BindingCount(bindingAction));
	ImGui::BeginDisabled(!bindingEditAllowed);
	if (ImGui::Button("Save bindings")) {
		if (input->SaveActionBindings(bindingPath, bindingStatus)) { bindingStatus = "Bindings saved"; }
	}
	ImGui::SameLine();
	if (ImGui::Button("Load bindings")) {
		if (input->LoadActionBindings(bindingPath, bindingStatus)) { bindingStatus = "Bindings loaded"; }
	}
	ImGui::EndDisabled();
	if (!bindingStatus.empty()) { ImGui::TextWrapped("%s", bindingStatus.c_str()); }
#endif
}

void Game::DrawProjectSettingsWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(!projectSettingsOpen_) return;
	ImGui::SetNextWindowSize({620,450},ImGuiCond_FirstUseEver);
	if (openProjectInputSettings_) ImGui::SetNextWindowFocus();
	if(ImGui::Begin("Project Settings",&projectSettingsOpen_)) {
		if (openProjectInputSettings_) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
		if (ImGui::CollapsingHeader("Input bindings")) DrawProjectInputSettings();
		openProjectInputSettings_ = false;
		ImGui::SeparatorText("Launch settings");
		ImGui::TextWrapped("Game output is shared by Editor and exported Player. Saving does not resize the current render targets. Restart Editor to preview the saved resolution.");
		const auto live=GetGameOutputExtent();
		ImGui::Text("Current session: %u x %u",live.width,live.height);
		if(projectSettingsContext_.IsOpen()) {
			const auto saved=projectSettingsContext_.GetSettings().gameOutput;
			ImGui::Text("Saved output: %u x %u",saved.width,saved.height);
			if(saved!=live) ImGui::TextUnformatted("Restart required to apply saved output in this Editor.");
		}
		ImGui::InputInt2("Game output (width / height)",projectOutputDraft_);
		const Engine::Base::ViewExtent draft{static_cast<uint32_t>(projectOutputDraft_[0]),static_cast<uint32_t>(projectOutputDraft_[1])};
		const bool valid=projectOutputDraft_[0]>0 && projectOutputDraft_[1]>0 && Engine::Base::ValidGameOutputExtent(draft);
		if(!valid) ImGui::TextUnformatted("Use dimensions 1..4096 and at most 8,388,608 pixels.");
		ImGui::Separator();
		const auto luaLive=scriptRuntime_.GetLuaResourceLimits();
		ImGui::Text("Current Lua: %zu bytes / %zu VMs / %zu opcodes per Update or Shutdown",luaLive.memoryBytes,luaLive.states,luaLive.frameInstructions);
		if(projectSettingsContext_.IsOpen()){
			const auto saved=projectSettingsContext_.GetSettings().luaLimits;
			ImGui::Text("Saved Lua: %zu bytes / %zu VMs / %zu opcodes",saved.memoryBytes,saved.states,saved.frameInstructions);
			if(saved.memoryBytes!=luaLive.memoryBytes || saved.states!=luaLive.states || saved.frameInstructions!=luaLive.frameInstructions)ImGui::TextUnformatted("Restart required to apply saved Lua limits.");
		}
		ImGui::InputInt3("Lua limits (bytes / VMs / opcodes)",projectLuaDraft_);
		const Engine::Scripting::LuaResourceLimits luaDraft{static_cast<size_t>(projectLuaDraft_[0]),static_cast<size_t>(projectLuaDraft_[1]),static_cast<size_t>(projectLuaDraft_[2])};
		const bool validLua=Engine::Base::ProjectContext::ValidLuaLimits(luaDraft);
		if(!validLua)ImGui::TextUnformatted("Lua: bytes 1..1,073,741,824; VMs 1..4096; opcodes 1..100,000,000.");
		ImGui::TextWrapped("Lua memory counts VM allocations and retained factory source bytes, not whole-process memory or wall-clock time. Save for next launch; running VMs are not reconfigured.");
		const bool busy=prefabRecoveryBlocked_ || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || assetImportJob_.IsBusy() ||
			worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || gizmoDragActive_;
		ImGui::BeginDisabled(busy);
		ImGui::InputText("Startup Scene (project-relative UTF-8)",projectStartupDraft_,sizeof(projectStartupDraft_));
		ImGui::TextWrapped("Changing startup Scene requires a saved generic Entity Scene inside this project. This does not switch the open document; restart to apply.");
		ImGui::BeginDisabled(!projectSettingsContext_.IsOpen());
		if(ImGui::Button("Browse Scene...")) {
			wchar_t file[32768]{};OPENFILENAMEW dialog{};
			dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=winApp->GetHwnd();
			dialog.lpstrFile=file;dialog.nMaxFile=32768;
			dialog.lpstrFilter=L"Scene (*.scene;*.json)\0*.scene;*.json\0All files (*.*)\0*.*\0\0";
			dialog.lpstrInitialDir=projectSettingsContext_.GetRootPath().c_str();
			dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
			if(GetOpenFileNameW(&dialog)) SelectStartupScene(file);
			else if(const auto code=CommDlgExtendedError()) projectSettingsStatus_="Scene dialog failed: "+std::to_string(code);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!genericSceneSelected_ || documentState_.IsDirty());
		if(ImGui::Button("Use current saved Scene")) SelectStartupScene(documentState_.Path());
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if(ImGui::Button("Reload saved settings")) ReloadProjectSettings();
		ImGui::SameLine();
		ImGui::BeginDisabled(!valid || !validLua || !projectSettingsContext_.IsOpen());
		if(ImGui::Button("Save for next launch")) {
			std::string error;
			const std::string startup=projectStartupDraft_;
			std::string validated;
			const bool unchanged=startup==projectSettingsContext_.GetSettings().startupScene;
			if(!unchanged && !Engine::Editor::SelectProjectStartupScene(projectSettingsContext_,startup,validated,error)) projectSettingsStatus_=error;
			else projectSettingsStatus_=projectSettingsContext_.SaveLaunchSettings(draft,unchanged ? startup : validated,luaDraft,error) ? "Saved. Restart Editor to apply output and Lua limits; exports use the saved settings." : error;
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if(busy) ImGui::TextUnformatted("Stop playback and wait for export/import or pending actions before saving.");
		ImGui::TextWrapped("%s",projectSettingsStatus_.c_str());
	}
	ImGui::End();
#endif
}

void Game::OpenExportWindow(bool native)
{
	ReloadExportSettings();
	if (native) exportNative_ = true;
	exportWindowOpen_ = true;
}

bool Game::ReloadExportSettings()
{
	if (exportJob_.GetState() == Engine::Services::ExportJob::State::Running || projectFile_.empty()) {
		exportStatus_ = "Open a project and wait for the current export before loading settings"; return false;
	}
	Engine::Editor::NativeBuildSettings settings;
	if (!Engine::Editor::NativeBuildSettingsFile::Load(Engine::Base::ProjectPaths(projectFile_.parent_path()).NativeBuildSettings(),settings,exportSettingsSnapshot_,exportStatus_)) return false;
	const auto copy = [](auto& destination, const std::string& source) { std::memcpy(destination,source.c_str(),source.size()+1); };
	copy(exportDirectory_,settings.output); copy(exportSdkDirectory_,settings.sdk); copy(exportNativeProject_,settings.project);
	exportNative_=settings.native; exportNativeDebug_=settings.debug;
	exportStatus_ = "Loaded local export settings (no build was started)"; return true;
}

bool Game::SaveExportSettings()
{
	if(!CheckPrefabRecovery(exportStatus_))return false;
	if (exportJob_.GetState() == Engine::Services::ExportJob::State::Running || projectFile_.empty()) {
		exportStatus_ = "Open a project and wait for the current export before saving settings"; return false;
	}
	Engine::Editor::NativeBuildSettings settings;
	settings.native=exportNative_; settings.debug=exportNativeDebug_; settings.sdk=exportSdkDirectory_;
	settings.project=exportNativeProject_; settings.output=exportDirectory_;
	if (!Engine::Editor::NativeBuildSettingsFile::SaveChecked(Engine::Base::ProjectPaths(projectFile_.parent_path()).NativeBuildSettings(),settings,exportSettingsSnapshot_,exportStatus_)) return false;
	exportStatus_ = "Saved UserSettings/native-build.json (local authoring only)"; return true;
}

void Game::DrawExportWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	const auto previous = exportJob_.GetState();
	exportJob_.Poll();
	const auto state = exportJob_.GetState();
	if (previous == Engine::Services::ExportJob::State::Running && state != previous) {
		exportStatus_ = state == Engine::Services::ExportJob::State::Succeeded ? "Export completed" :
			state == Engine::Services::ExportJob::State::Cancelled ? "Export cancelled; inspect any partial output and retained temporary build stage" :
			"Export failed (exit " + std::to_string(exportJob_.GetExitCode()) + ")";
		exportLog_ = exportJob_.ReadLog();
	}
	if (!exportWindowOpen_) return;
	ImGui::SetNextWindowSize({620.0f, 420.0f}, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Export Game", &exportWindowOpen_)) {
		ImGui::TextWrapped("Export saved project files. Save your scene and assets before exporting. No Visual Studio GUI is required.");
		ImGui::TextWrapped("Choose a new or empty folder. Relative paths are resolved from the project root.");
		const bool running = state == Engine::Services::ExportJob::State::Running;
		ImGui::BeginDisabled(running);
		ImGui::Checkbox("Build native C++ game from SDK", &exportNative_);
		if (exportNative_) {
			ImGui::InputText("Runtime SDK directory (UTF-8)", exportSdkDirectory_, sizeof(exportSdkDirectory_));
			ImGui::InputText("Native .vcxproj (UTF-8)", exportNativeProject_, sizeof(exportNativeProject_));
			ImGui::Checkbox("Debug native build (otherwise Release)", &exportNativeDebug_);
			ImGui::TextWrapped("Requires compatible C++ Build Tools. Only use a trusted SDK and MSBuild project: its targets can execute code. Save local settings explicitly to retain these paths.");
		} else ImGui::TextWrapped("Uses the prebuilt Release Player; no C++ compiler is required.");
		ImGui::InputText("Output directory (UTF-8)", exportDirectory_, sizeof(exportDirectory_));
		ImGui::BeginDisabled(projectFile_.empty());
		if (ImGui::Button("Save local build settings")) SaveExportSettings();
		ImGui::SameLine();
		if (ImGui::Button("Reload local build settings")) ReloadExportSettings();
		ImGui::EndDisabled();
		ImGui::TextWrapped("Save refuses externally changed settings. Reload replaces this draft with the latest local settings.");
		const auto* sample = genericSceneSelected_ ? nullptr : dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
		const bool playing = worldPlayback_.GetState() != WorldPlayback::State::Editing ||
			(sample && sample->GetPlayback() != GamePlayScene::Playback::Editing);
		const bool unsaved = documentState_.IsDirty() || (sample && sample->HasUnsavedScene());
		const char* unavailable = EditorDocumentBusyReason();
		if (!unavailable) {
			if (projectFile_.empty()) unavailable = "Open a .kubo project from the Hub first.";
			else if (playing) unavailable = "Stop playback before exporting.";
			else if (unsaved) unavailable = "Save the current Scene before exporting.";
			else if (!exportDirectory_[0]) unavailable = "Choose an output directory.";
			else if (exportNative_ && (!exportSdkDirectory_[0] || !exportNativeProject_[0]))
				unavailable = "Choose the Runtime SDK and native .vcxproj paths.";
		}
		ImGui::BeginDisabled(unavailable != nullptr);
		if (ImGui::Button(exportNative_ ? "Build and export native game" : "Export saved project")) {
			try {
				if(!CheckPrefabRecovery(exportStatus_))throw std::runtime_error(exportStatus_);
				const auto projectPath = [&](const char* text) {
					std::u8string utf8(std::strlen(text), u8'\0');
					std::memcpy(utf8.data(), text, utf8.size());
					auto path = std::filesystem::path(utf8);
					return path.is_relative() ? projectFile_.parent_path() / path : path;
				};
				const auto output = projectPath(exportDirectory_);
				bool started = false;
				if (exportNative_) {
					started = exportJob_.StartNative(projectFile_, output, projectPath(exportSdkDirectory_),
						projectPath(exportNativeProject_), exportNativeDebug_, exportStatus_);
				} else {
					wchar_t executable[32768]{};
					const auto length = GetModuleFileNameW(nullptr, executable, 32768);
					if (!length || length == 32768) throw std::runtime_error("Cannot locate engine installation");
					const auto installation = std::filesystem::path(executable).parent_path();
					auto player = installation / "KuboPlayer.exe";
					if (!std::filesystem::is_regular_file(player)) player = installation.parent_path() / "Release/KuboPlayer.exe";
					started = exportJob_.Start(projectFile_, output, installation / "tools/package_project.ps1", player, exportStatus_);
				}
				if (started) {
					exportStatus_ = exportNative_ ? "Building and exporting native game..." : "Exporting...";
					exportLog_.clear();
				}
			} catch (const std::exception& e) { exportStatus_ = e.what(); }
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if(running) {
			ImGui::BeginDisabled(exportJob_.IsCancellationRequested());
			if(ImGui::Button("Cancel build/export")) {
				if(exportJob_.Cancel(exportStatus_)) exportStatus_="Cancellation requested; waiting for owned child processes to stop...";
			}
			ImGui::EndDisabled();
		}
		if(!exportJob_.GetControlError().empty()) ImGui::TextWrapped("%s",exportJob_.GetControlError().c_str());
		if (unavailable) ImGui::TextWrapped("%s", unavailable);
		ImGui::TextWrapped("%s", exportStatus_.c_str());
		if (!exportJob_.GetLogPath().empty()) {
			const auto logPath = exportJob_.GetLogPath().u8string();
			ImGui::TextWrapped("Log: %s", reinterpret_cast<const char*>(logPath.c_str()));
			if (ImGui::Button("Refresh log")) exportLog_ = exportJob_.ReadLog();
		}
		ImGui::BeginChild("Export log", {0, 0}, true);
		ImGui::TextUnformatted(exportLog_.c_str());
		ImGui::EndChild();
	}
	ImGui::End();
#endif
}

void Game::InitializeGenericEditorWorld()
{
	entityHistory_.Clear();
	editorWorld_.Clear();
	const auto root = editorWorld_.CreateEntity("Gameplay World");
	const auto player = editorWorld_.CreateEntity("Player", root);
	genericPlayerEntity_ = player;
	editorWorld_.SetModel(player, "Resources/models/walk/walk.gltf");
	editorWorld_.SetCollider(player, {{0.45f, 0.9f, 0.45f}, false});
	editorWorld_.SetRigidBody(player, {{0.0f, 0.0f, 0.0f}, 1.0f, true, true, false});
	const auto terrain = editorWorld_.CreateEntity("Terrain", root);
	editorWorld_.Find(terrain)->transform.position = {0.0f, -1.0f, 0.0f};
	editorWorld_.SetCollider(terrain, {{8.0f, 0.5f, 8.0f}, false});
	selectedEditorEntity_ = player;
	bool loadedFromDisk = false;
	std::error_code fileError;
	if (std::filesystem::exists(genericWorldPath_, fileError)) {
		std::string error;
		if (editorWorld_.Load(genericWorldPath_, error)) {
			loadedFromDisk = true;
			genericWorldStatus_ = "Loaded: " + genericWorldPath_.generic_string();
			selectedEditorEntity_ = editorWorld_.GetEntities().empty() ? 0 : editorWorld_.GetEntities().front().id;
			genericPlayerEntity_ = 0;
			for (const auto& entity : editorWorld_.GetEntities()) {
				if (entity.name == "Player") { genericPlayerEntity_ = entity.id; break; }
			}
		} else {
			genericWorldStatus_ = "Load failed: " + error;
		}
	}
	if (!loadedFromDisk) {
		if (auto* scene = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
			const EulerTransform transform = scene->GetPlayerTransform();
			if (auto* playerEntity = editorWorld_.Find(player)) {
				playerEntity->transform.position = transform.translate;
				playerEntity->transform.rotation = transform.rotate;
				playerEntity->transform.scale = transform.scale;
			}
		}
	}
	documentState_.Opened(editorWorld_, genericWorldPath_);
}

bool Game::DrawHierarchyTree()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) hierarchyPressedEntity_ = 0;
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##HierarchySearch","Search Entities",hierarchySearch_,sizeof(hierarchySearch_));
#if defined(KUBO_APPLICATION_CHECKS)
	Engine::Editor::ObserveRenderInspectorChecks("Hierarchy search");
#endif
	static constexpr std::pair<HierarchyType, const char*> types[] = {
		{HierarchyType::All,"All types"}, {HierarchyType::Model,"Model"}, {HierarchyType::Camera,"Camera"},
		{HierarchyType::Light,"Light"}, {HierarchyType::Canvas,"Canvas"}, {HierarchyType::Sprite,"Sprite"},
		{HierarchyType::Particle,"Particle"}, {HierarchyType::Audio,"Audio"}, {HierarchyType::Script,"Script"},
		{HierarchyType::Physics,"Physics"}, {HierarchyType::Prefab,"Prefab"},
	};
	const auto selectedType = std::find_if(std::begin(types),std::end(types),[&](const auto& type) {
		return type.first == hierarchyType_;
	});
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
	const bool typeMenuOpen = ImGui::BeginCombo("##HierarchyType",selectedType->second);
#if defined(KUBO_APPLICATION_CHECKS)
	if (!typeMenuOpen) Engine::Editor::ObserveRenderInspectorChecks("Hierarchy type filter");
#endif
	if (typeMenuOpen) {
		for (const auto& [type,label] : types) {
			if (ImGui::Selectable(label,type == hierarchyType_)) hierarchyType_ = type;
#if defined(KUBO_APPLICATION_CHECKS)
			Engine::Editor::ObserveRenderInspectorChecks(label);
#endif
			if (type == hierarchyType_) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!HasHierarchyFilter());
	if (ImGui::Button("Clear filters")) {
		hierarchySearch_[0] = '\0';
		hierarchyType_ = HierarchyType::All;
	}
#if defined(KUBO_APPLICATION_CHECKS)
	Engine::Editor::ObserveRenderInspectorChecks("Clear hierarchy filters");
#endif
	ImGui::EndDisabled();
	const auto& entities = editorWorld_.GetEntities();
	const bool filtering = HasHierarchyFilter();
	std::unordered_map<EntityId,const EntityRecord*> records;
	for (const auto& entity : entities) records.emplace(entity.id,&entity);
	std::unordered_set<EntityId> matches, included;
	if (filtering) {
		for (const auto& entity : entities) {
			if (!MatchesHierarchyFilter(entity)) continue;
			matches.insert(entity.id);
			for (auto* node = &entity; node && included.insert(node->id).second;) {
				const auto parent = records.find(node->parent);
				node = parent == records.end() ? nullptr : parent->second;
			}
		}
	}
	ImGui::Text("%zu / %zu Entities, %zu selected",filtering ? matches.size() : entities.size(),entities.size(),editorSelection_.Ids().size());
	if (HasHierarchyHiddenSelection()) ImGui::TextWrapped("Some selected Entities are hidden by the filters. Select a result or clear the filters to edit.");
	if (filtering && matches.empty()) { ImGui::TextDisabled("No matching Entities."); return false; }
	if (entities.empty()) { ImGui::TextDisabled("The Scene has no Entities."); return false; }

	std::unordered_map<EntityId,std::vector<const EntityRecord*>> children;
	for (const auto& entity : entities) {
		if (!filtering || included.contains(entity.id))
			children[records.contains(entity.parent) ? entity.parent : 0].push_back(&entity);
	}
	struct Row { const EntityRecord* entity; unsigned depth; };
	std::vector<Row> pending;
	const auto appendRows = [&](EntityId parent, unsigned depth) {
		const auto list = children.find(parent);
		if (list == children.end()) return;
		for (auto child = list->second.rbegin(); child != list->second.rend(); ++child) pending.push_back({*child,depth});
	};
	appendRows(0,0);
	std::vector<EntityId> displayed;
	EntityId clicked = 0;
	bool extend = false, range = false;
	std::optional<Engine::Editor::SceneEditAction> command;
	std::optional<EntityId> reparentTo;
	using Action = Engine::Editor::SceneEditAction;
	const bool canReparent = !SceneEditUnavailableReason(Action::Duplicate);
	const auto dropTarget = [&](EntityId parent) {
		if (!canReparent || !ImGui::BeginDragDropTarget()) return;
		if (const auto* payload = ImGui::AcceptDragDropPayload("KUBO_ENTITY_SELECTION");
			payload && payload->IsDelivery() && payload->DataSize == sizeof(EntityId)) {
			EntityId source = 0;
			std::memcpy(&source,payload->Data,sizeof(source));
			if (editorSelection_.Contains(source)) reparentTo = parent;
		}
		ImGui::EndDragDropTarget();
	};
	ImGui::Selectable("Scene root##DropTarget",false,ImGuiSelectableFlags_NoAutoClosePopups);
	dropTarget(0);
	// Search expansion uses a separate ID scope so it cannot overwrite the ordinary tree.
	ImGui::PushID(filtering ? "Filtered hierarchy" : "Hierarchy tree");
	while (!pending.empty()) {
		const auto [entity,depth] = pending.back();
		pending.pop_back();
		const auto id = entity->id;
		const bool selectable = !filtering || matches.contains(id);
		const bool selected = editorSelection_.Contains(id);
		const bool branch = children.contains(id);
		const auto key = std::to_string(id);
		ImGui::PushID(key.c_str());
		const float indent = (std::min)(depth,10u) * ImGui::GetStyle().IndentSpacing;
		if (indent > 0) ImGui::Indent(indent);
		if (filtering && branch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
		if (!branch) flags |= ImGuiTreeNodeFlags_Leaf;
		if (selected) flags |= ImGuiTreeNodeFlags_Selected;
		const bool muted = !selectable || !entity->active;
		if (muted) ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		const char* kind = !entity->prefabLink.is_null() ? "Prefab" : entity->hasCamera ? "Camera" :
			entity->hasLight ? "Light" : entity->hasCanvas ? "Canvas" : entity->hasSprite ? "Sprite" :
			entity->hasModel ? "Model" : entity->hasParticle ? "Particle" : "";
		const bool open = ImGui::TreeNodeEx("##Entity",flags,"%s%s%s",entity->name.c_str(),*kind ? "  /  " : "",kind);
#if defined(KUBO_APPLICATION_CHECKS)
		Engine::Editor::ObserveRenderInspectorChecks(entity->name.c_str());
#endif
		if (muted) ImGui::PopStyleColor();
		if (selectable) {
			displayed.push_back(id);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) hierarchyPressedEntity_ = id;
			if (hierarchyPressedEntity_ == id && ImGui::IsItemHovered() &&
				ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload()) {
				clicked = id;
				extend = ImGui::GetIO().KeyCtrl;
				range = ImGui::GetIO().KeyShift;
			}
		}
		if (selectable && selected && canReparent && ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("KUBO_ENTITY_SELECTION",&id,sizeof(id));
			ImGui::Text("Move %zu selected",editorSelection_.Ids().size());
			ImGui::EndDragDropSource();
		}
		dropTarget(id);
		if (selectable && ImGui::BeginPopupContextItem("Entity actions")) {
			if (!selected && ImGui::IsWindowAppearing()) { clicked = id; extend = range = false; }
			ImGui::TextUnformatted(entity->name.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate selected subtrees",nullptr,false,selected && canReparent)) command = Action::Duplicate;
			if (ImGui::MenuItem("Delete selected subtrees",nullptr,false,selected && canReparent)) command = Action::Delete;
			if (ImGui::MenuItem("Move selection to Scene root",nullptr,false,selected && canReparent)) reparentTo = 0;
			ImGui::EndPopup();
		}
		if (indent > 0) ImGui::Unindent(indent);
		ImGui::PopID();
		if (open && branch) appendRows(id,depth+1);
	}
	ImGui::PopID();
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) hierarchyPressedEntity_ = 0;
	std::string error;
	if (command) return RunSceneEditAction(*command);
	if (reparentTo) return ReparentEditorSelection(*reparentTo,keepWorldOnReparent_,error);
	if (clicked) {
		if (SelectEditorEntityFromRows(clicked,extend,range,error,displayed)) return true;
		genericWorldStatus_ = error;
	}
#endif
	return false;
}

void Game::DrawInspectorAssetDropTarget(const EntityRecord& entity)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	const auto* blocked = SceneEditUnavailableReason(Engine::Editor::SceneEditAction::Duplicate);
	ImGui::BeginDisabled(blocked != nullptr);
	ImGui::Button("Drop Model or Sprite Image",{-1,0});
#if defined(KUBO_APPLICATION_CHECKS)
	Engine::Editor::ObserveRenderInspectorChecks("Inspector asset drop");
#endif
	ImGui::EndDisabled();
	if (blocked) return;
	const auto guid = GetAssetPayloadGuid(ImGui::GetDragDropPayload());
	const auto* asset = guid.empty() ? nullptr : projectAssets_.FindByGuid(guid);
	if (!asset || (asset->type != Assets::AssetType::Model && asset->type != Assets::AssetType::Texture) ||
		!ImGui::BeginDragDropTarget()) return;
	const auto* payload = ImGui::AcceptDragDropPayload(Engine::Editor::AssetGuidPayload);
	const bool deliver = payload && payload->IsDelivery();
	ImGui::EndDragDropTarget();
	if (!deliver) return;
	std::string error;
	const auto source = Assets::ResolveAssetReference({},guid,asset->type,projectAssets_,GetProjectRoot(),error);
	const auto relative = source.lexically_relative(GetProjectRoot());
	if (source.empty() || relative.empty() || *relative.begin() == L"..") {
		genericWorldStatus_ = error.empty() ? "Asset must be inside the project" : error;
		return;
	}
	const auto hint = Base::PathToUtf8(relative);
	if (asset->type == Assets::AssetType::Model) {
		if (!editorWorld_.SetModel(entity.id,hint,guid)) genericWorldStatus_ = "Model assignment rejected";
	} else {
		auto sprite = entity.sprite;
		sprite.assetPath = hint;
		sprite.assetGuid = guid;
		if (!editorWorld_.SetSprite(entity.id,sprite)) genericWorldStatus_ = "Sprite image assignment rejected";
	}
#else
	(void)entity;
#endif
}

void Game::DrawAddComponentMenu(const EntityRecord& entity)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	const auto* unavailable = SceneEditUnavailableReason(Engine::Editor::SceneEditAction::Duplicate);
	ImGui::BeginDisabled(unavailable != nullptr);
	if (ImGui::Button("Add Component...")) ImGui::OpenPopup("Add Component");
#if defined(KUBO_APPLICATION_CHECKS)
	Engine::Editor::ObserveRenderInspectorChecks("Add Component...");
#endif
	ImGui::EndDisabled();
	if (unavailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s",unavailable);
	if (!ImGui::BeginPopup("Add Component")) return;
	if (unavailable) ImGui::CloseCurrentPopup();
	static ImGuiTextFilter search;
	if (ImGui::IsWindowAppearing()) {
		search.Clear();
		ImGui::SetKeyboardFocusHere();
	}
	search.Draw("##ComponentSearch",ImGui::GetFontSize()*13);
	bool matches = false;
	const auto option = [&](const char* name, bool installed, auto setter) {
		if (!search.PassFilter(name)) return;
		matches = true;
		if (ImGui::Selectable(name,false,installed || unavailable ? ImGuiSelectableFlags_Disabled : 0) &&
			!(editorWorld_.*setter)(entity.id,{}))
			genericWorldStatus_ = std::string("Could not add ") + name;
#if defined(KUBO_APPLICATION_CHECKS)
		Engine::Editor::ObserveRenderInspectorChecks(name);
#endif
		if (installed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Already added");
	};
	option("Animator",entity.hasAnimator,&EntityWorld::SetAnimator);
	option("Audio Source",entity.hasAudio,&EntityWorld::SetAudio);
	option("Camera",entity.hasCamera,&EntityWorld::SetCamera);
	option("Canvas",entity.hasCanvas,&EntityWorld::SetCanvas);
	option("Collider",entity.hasCollider,&EntityWorld::SetCollider);
	option("Light",entity.hasLight,&EntityWorld::SetLight);
	option("Material",entity.hasMaterial,&EntityWorld::SetMaterial);
	option("Particle",entity.hasParticle,&EntityWorld::SetParticle);
	option("Rigid Body",entity.hasRigidBody,&EntityWorld::SetRigidBody);
	option("Sprite",entity.hasSprite,&EntityWorld::SetSprite);
	if (!matches) ImGui::TextDisabled("No matching components");
	ImGui::EndPopup();
#else
	(void)entity;
#endif
}

void Game::DrawGenericEntityEditor()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	using Action=Engine::Editor::SceneEditAction;
	using Shortcut=Engine::Editor::DocumentShortcuts::Command;
	auto shortcut=Engine::Editor::DocumentShortcuts::Take("Entity Hierarchy",genericHierarchyOpen_,true,true);
	if(shortcut==Shortcut::None)shortcut=Engine::Editor::DocumentShortcuts::Take("Entity Inspector",genericInspectorOpen_,true);
	if(shortcut==Shortcut::None)shortcut=Engine::Editor::DocumentShortcuts::Take("Scene",debugSceneViewOpen_,true);
	switch (shortcut) {
	case Shortcut::Undo: RunSceneEditAction(Action::Undo); break;
	case Shortcut::Redo: RunSceneEditAction(Action::Redo); break;
	case Shortcut::Duplicate: RunSceneEditAction(Action::Duplicate); break;
	case Shortcut::Delete: RunSceneEditAction(Action::Delete); break;
	case Shortcut::None: break;
	}
	auto beforeEdit = editorWorld_.Capture();
	ImGui::BeginDisabled(prefabRecoveryBlocked_ || gizmoDragActive_);
	auto beforeSelection = editorSelection_.Capture();
	const auto resetBaseline = [&]() { beforeEdit = editorWorld_.Capture();editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);beforeSelection = editorSelection_.Capture(); };
	const auto editButton = [&](const char* label, Action action, const char* hint = nullptr) {
		const char* unavailable = SceneEditUnavailableReason(action);
		ImGui::BeginDisabled(unavailable != nullptr);
		if (ImGui::Button(label) && RunSceneEditAction(action)) resetBaseline();
		ImGui::EndDisabled();
		if (const char* tooltip = unavailable ? unavailable : hint;
			tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",tooltip);
	};
	if (genericHierarchyOpen_) {
		ImGui::SetNextWindowSize({300.0f, 420.0f}, ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Entity Hierarchy", &genericHierarchyOpen_)) {
			const auto documentLabel=genericWorldPath_.empty() ? std::string("Untitled Scene") : Engine::Base::PathToUtf8(genericWorldPath_.filename());
			ImGui::TextWrapped("%s%s",documentLabel.c_str(),documentState_.IsDirty() ? " *" : "");
			if (!genericWorldPath_.empty() && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s",Engine::Base::PathToUtf8(genericWorldPath_).c_str());
			ImGui::BeginDisabled(documentState_.HasPendingAction());
			ImGui::TextUnformatted(documentState_.IsDirty() ? "Scene: unsaved changes *" : "Scene: saved baseline");
			ImGui::BeginDisabled(worldPlayback_.GetState() != WorldPlayback::State::Editing);
			editButton("Undo",Action::Undo);
			ImGui::SameLine();
			editButton("Redo",Action::Redo);
			// The outer disabled scope was computed before Undo/Redo. Recheck
			// after replay so retained pending data locks this same UI frame.
			ImGui::BeginDisabled(prefabRecoveryBlocked_);
			if (ImGui::Button("+ Entity")) {
				EntityId created=0;std::string error;
				if(CreateEditorEntity("Entity",0,created,error))resetBaseline();
			}
			ImGui::SameLine();
			editButton("Duplicate selected subtrees",Action::Duplicate,"Ctrl+D: copy selected subtrees as one Undo command.");
			editButton("Delete selected subtrees",Action::Delete,"Delete in Hierarchy: remove selected Entities and descendants. Asset files are kept.");
			if(ImGui::CollapsingHeader("Prefab assets")) {
			static char prefabPath[1024]="New.prefab";
			static bool prefabUnderPrimary=false;
			ImGui::InputText("New Prefab path (asset-relative UTF-8)",prefabPath,sizeof(prefabPath));
			std::string prefabGuard;const bool prefabAllowed=!HasHierarchyHiddenSelection() && CanAuthorPrefab(prefabGuard);
			ImGui::BeginDisabled(!prefabAllowed || !editorWorld_.Find(selectedEditorEntity_));
			if(ImGui::Button("Save primary subtree as new Prefab")) {
				std::filesystem::path saved;std::string error;
				SaveEditorPrefab(Engine::Base::PathFromUtf8(prefabPath),saved,error);
			}
			ImGui::EndDisabled();
			ImGui::TextWrapped("New file only; original stays unchanged. Refresh Asset Browser to register its GUID.");
			if(ImGui::Button("Open Asset Browser for Prefabs"))debugAssetBrowserOpen_=true;
			ImGui::Checkbox("Place Prefab under primary (local TRS)",&prefabUnderPrimary);
			ImGui::BeginDisabled(!prefabAllowed || (prefabUnderPrimary && !editorWorld_.Find(selectedEditorEntity_)));
			std::string placePrefabGuid;
			if(ImGui::BeginCombo("Place linked Prefab","Choose registered source...")) {
				for(const auto* asset:projectAssets_.Search("",Engine::Assets::AssetType::Prefab)) {
					std::error_code identityError;
					if(!std::filesystem::is_regular_file(Engine::Assets::AssetRegistry::MetadataPath(Engine::Base::PathFromUtf8(asset->absolutePath)),identityError))continue;
					const auto label=asset->relativePath+"##prefab-"+asset->guid;
					if(ImGui::Selectable(label.c_str()))placePrefabGuid=asset->guid;
				}
				ImGui::EndCombo();
			}
			if(!placePrefabGuid.empty()) {EntityId root=0;std::string error;if(InstantiateEditorPrefab(placePrefabGuid,prefabUnderPrimary?selectedEditorEntity_:0,root,error))resetBaseline();}
			const auto* prefabRoot=editorWorld_.Find(selectedEditorEntity_);
			ImGui::BeginDisabled(!prefabRoot || prefabRoot->prefabLink.is_null());
			if(ImGui::Button("Apply primary Prefab root...")) {
				std::string error;editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
				// Settle this frame's ordinary edits before freezing history/doc.
				if(entityHistory_.Record(beforeEdit,editorWorld_,beforeSelection,editorSelection_.Capture(),error)){
					entityHistory_.Commit();documentState_.Observe(editorWorld_);resetBaseline();RequestEditorPrefabApply(selectedEditorEntity_,error);
				}else genericWorldStatus_="Apply preview rejected: "+error;
			}
			if(ImGui::Button("Revert primary Prefab root...")) {
				std::string error;if(!RequestEditorPrefabRevert(selectedEditorEntity_,error))genericWorldStatus_="Revert preview rejected: "+error;
			}
			if(ImGui::Button("Inspect primary Prefab overrides")){std::string error;if(!InspectEditorPrefabOverrides(selectedEditorEntity_,error))genericWorldStatus_="Prefab inspection rejected: "+error;}
			if(ImGui::Button("Compare primary Prefab with current source")){std::string error;if(!InspectEditorPrefabThreeWay(selectedEditorEntity_,error))genericWorldStatus_="Prefab comparison rejected: "+error;}
			if(ImGui::Button("Unlink primary Prefab root")){std::string error;if(UnlinkEditorPrefab(selectedEditorEntity_,error))resetBaseline();}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			ImGui::TextWrapped("Placement/unlink/Revert/Apply: one Undo command. Apply updates the source and matching roots in this Scene, preserving independent overrides. Closed Scenes and nested Prefabs are not propagated.");
			}
			if(prefabApply_ && !prefabApply_->field && !ImGui::IsPopupOpen("Confirm Prefab Apply"))ImGui::OpenPopup("Confirm Prefab Apply");
			if(ImGui::BeginPopupModal("Confirm Prefab Apply",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
				if(prefabRecoveryBlocked_ || !prefabApply_ || prefabApply_->field){if(!prefabApply_ || !prefabApply_->field)CancelEditorPrefabApply();ImGui::CloseCurrentPopup();}
				else {
					ImGui::TextWrapped("Source: %s",Engine::Base::PathToUtf8(prefabApply_->revision.file).c_str());
					ImGui::Text("Root: %llu; matching instances: %zu",static_cast<unsigned long long>(prefabApply_->root),prefabApply_->instances.size());
					ImGui::TextUnformatted(prefabApply_->admission.SourceChanged()?"Source file and this Scene will change":"Source unchanged: Scene baseline refresh or exact no-op");
					for(const auto& instance:prefabApply_->instances)ImGui::Text("Root %llu: inherited %zu, overrides %zu, additions %zu, removals %zu",static_cast<unsigned long long>(instance.root),instance.merge.inherited.size(),instance.merge.overrides.size(),instance.merge.added.size(),instance.merge.removed.size());
					ImGui::TextWrapped("The elected instance defines the next source. Other roots keep independent overrides; existing names remain instance-owned. Source/Scene/selection/document/history are revalidated before publication. Undo restores the source pair and Scene. Scene files are not saved and closed Scenes are not rewritten. Pending recovery blocks further authoring if final archive fails.");
					const bool replacement=prefabApply_->sourceReview.RequiresApproval();
					if(replacement){
						auto& pending=*prefabApply_;const auto& rows=pending.sourceReview.replacements;
						constexpr size_t PageSize=16;const auto pages=(rows.size()+PageSize-1)/PageSize;pending.reviewPage=(std::min)(pending.reviewPage,int(pages-1));
						ImGui::Text("Current-source changes to replace: %zu; page %d / %zu",rows.size(),pending.reviewPage+1,pages);
						ImGui::BeginDisabled(pending.reviewPage==0);if(ImGui::Button("Previous source replacements"))--pending.reviewPage;ImGui::EndDisabled();ImGui::SameLine();
						ImGui::BeginDisabled(size_t(pending.reviewPage+1)>=pages);if(ImGui::Button("Next source replacements"))++pending.reviewPage;ImGui::EndDisabled();
						ImGui::BeginChild("Source replacements",ImVec2(720,240),true);
						for(size_t index=size_t(pending.reviewPage)*PageSize;index<(std::min)(rows.size(),size_t(pending.reviewPage+1)*PageSize);++index){
							const auto& row=rows[index];const auto& entry=row.comparison;const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(entry.path,1024);
							ImGui::Text("Local %llu / Scene %llu | %.*s",static_cast<unsigned long long>(entry.local),static_cast<unsigned long long>(entry.scene),static_cast<int>(bytes),entry.path.c_str());
							const auto show=[](const char* label,const std::optional<std::string>& value){ImGui::TextUnformatted(label);if(value){const auto count=Engine::Editor::PrefabOverrideDisplayPrefix(*value,2048);ImGui::TextWrapped("%.*s",static_cast<int>(count),value->c_str());if(count<value->size())ImGui::TextUnformatted("(display truncated)");}else ImGui::TextUnformatted("(absent)");};
							show("Stored baseline",entry.baseline);show("Current source",entry.source);show("Elected instance",entry.instance);show("Proposed source",row.proposed);ImGui::Separator();
						}ImGui::EndChild();
						ImGui::Checkbox("Approve replacing ALL listed current-source changes with this proposed Apply source",&pending.conflictApproved);
					}
					ImGui::BeginDisabled(replacement && !prefabApply_->conflictApproved);
					if(ImGui::Button("Confirm Apply")){const bool approved=prefabApply_->conflictApproved;std::string error;if(ConfirmEditorPrefabApply(error,approved))resetBaseline();ImGui::CloseCurrentPopup();}
					ImGui::EndDisabled();
					ImGui::SameLine();if(ImGui::Button("Cancel Apply")){CancelEditorPrefabApply();ImGui::CloseCurrentPopup();}
				}
				ImGui::EndPopup();
			}
			if(HasPendingPrefabRevert() && !ImGui::IsPopupOpen("Confirm Prefab Revert"))ImGui::OpenPopup("Confirm Prefab Revert");
			if(ImGui::BeginPopupModal("Confirm Prefab Revert",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
				if(prefabRecoveryBlocked_ || !HasPendingPrefabRevert()){
					CancelEditorPrefabRevert();ImGui::CloseCurrentPopup();
				}else{
				ImGui::TextWrapped("Restore the current source's names, active state, local transforms (including root), components and asset references. Instance-added descendants are removed; deleted source children are restored. External parent and existing mapped IDs are retained. Undo is available. No source file is written.");
				ImGui::Text("Target Scene Entity: %llu",static_cast<unsigned long long>(pendingPrefabRevertRoot_));
				if(ImGui::Button("Confirm Revert")) {
					std::string error;
					if(ConfirmEditorPrefabRevert(error))resetBaseline();else genericWorldStatus_="Prefab Revert cancelled: "+error;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if(ImGui::Button("Cancel Revert")) {
					CancelEditorPrefabRevert();ImGui::CloseCurrentPopup();
				}
				}
				ImGui::EndPopup();
			}
			ImGui::BeginDisabled(SceneEditUnavailableReason(Action::Duplicate) != nullptr);
			ImGui::Checkbox("Keep World on batch reparent",&keepWorldOnReparent_);
			bool batchParentChosen=false;EntityId batchParent=0;
			ImGui::SetNextItemWidth(-1.0f);
			if(ImGui::BeginCombo("##Reparent selected subtrees","Reparent selection...")) {
				if(ImGui::Selectable("Scene root (None)")){batchParentChosen=true;batchParent=0;}
				for(const auto& candidate:editorWorld_.GetEntities()) {
					if(editorSelection_.Contains(candidate.id))continue;
					const auto label=candidate.name+" ("+std::to_string(candidate.id)+")##batch-parent-"+std::to_string(candidate.id);
					if(ImGui::Selectable(label.c_str())){batchParentChosen=true;batchParent=candidate.id;}
				}
				ImGui::EndCombo();
			}
			if(ImGui::IsItemHovered())ImGui::SetTooltip("Move selected forest roots only; selected descendants stay attached. Destination inside the forest is rejected; one Undo command.");
			// Publish after iteration: a successful command invalidates Entity pointers.
			if(batchParentChosen){std::string error;if(ReparentEditorSelection(batchParent,keepWorldOnReparent_,error))resetBaseline();}
			ImGui::EndDisabled();
			ImGui::SameLine();
			const auto* saveBusy = EditorDocumentBusyReason();
			ImGui::BeginDisabled(saveBusy != nullptr);
			if (ImGui::Button("Save Scene")) {
				SaveGenericDocument();
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s",saveBusy ? saveBusy : "Save Scene (Ctrl+S)");
			ImGui::SameLine();
			ImGui::BeginDisabled(genericWorldPath_.empty());
			if (ImGui::Button("Load Scene")) {
				RequestDocumentAction(Engine::Editor::EntityDocumentState::Action::Reload);
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled(); // post-history recovery scope
			ImGui::EndDisabled();
			if (!genericWorldStatus_.empty()) { ImGui::TextWrapped("%s", genericWorldStatus_.c_str()); }
			ImGui::Separator();
			if(ImGui::Button("Clear selection")) {std::string error;if(SelectEditorEntity(0,false,false,error))resetBaseline();}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+click: toggle\nShift+click: select range\nCtrl+Shift+click: add range");
			if (DrawHierarchyTree()) resetBaseline();
			ImGui::EndDisabled();
		}
		ImGui::End();
	}

	if (genericInspectorOpen_) {
		ImGui::SetNextWindowSize({360.0f, 420.0f}, ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Entity Inspector", &genericInspectorOpen_)) {
			const auto& inspectorSelection = GetEditorSelection();
			const bool multi = genericSceneSelected_ && inspectorSelection.size() > 1;
			if (multi) ImGui::Text("%zu selected. Name, Transform and components edit the primary Entity.",inspectorSelection.size());
			const bool hiddenSelection = HasHierarchyHiddenSelection();
			if (hiddenSelection) ImGui::TextWrapped("Clear Hierarchy filters or select visible Entities to edit.");
			ImGui::BeginDisabled(prefabRecoveryBlocked_ || documentState_.HasPendingAction() || hiddenSelection);
			EntityRecord* entity = editorWorld_.Find(selectedEditorEntity_);
			if (!entity) {
				ImGui::TextUnformatted("Select an entity from the hierarchy.");
			} else {
				DrawAddComponentMenu(*entity);
				DrawInspectorAssetDropTarget(*entity);
				char name[128]{};
				if(!entity->prefabLink.is_null())ImGui::TextWrapped("Linked Prefab source GUID: %s",entity->prefabLink["sourceGuid"].get<crude_json::string>().c_str());
				strncpy_s(name, entity->name.c_str(), _TRUNCATE);
				if (ImGui::InputText("Name", name, sizeof(name))) {
					entity->name = name;
				}
				if (multi) {
					bool active = entity->active;
					const bool mixed = std::any_of(inspectorSelection.begin(),inspectorSelection.end(),[&](EntityId id) {
						const auto* target = editorWorld_.Find(id);
						return target && target->active != active;
					});
					ImGui::BeginDisabled(SceneEditUnavailableReason(Action::Duplicate) != nullptr);
					ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,mixed);
					if (Engine::Editor::RenderInspectorCheckbox("Active (selection)",&active)) {
						for (const auto id : inspectorSelection) {
							if (auto* target = editorWorld_.Find(id)) target->active = mixed || active;
						}
					}
					ImGui::PopItemFlag();
					ImGui::EndDisabled();
				} else ImGui::Checkbox("Active", &entity->active);
				const auto* parentEntity = editorWorld_.Find(entity->parent);
				ImGui::Checkbox("Keep World Transform", &keepWorldOnReparent_);
				const auto changeParent=[&](EntityId parent) {
					std::string error;
					if(!editorWorld_.SetParent(entity->id,parent,keepWorldOnReparent_,error)) genericWorldStatus_=error;
				};
				if (ImGui::BeginCombo("Parent", parentEntity ? parentEntity->name.c_str() : "None")) {
					if (ImGui::Selectable("None", entity->parent == 0)) changeParent(0);
					for (const auto& candidate : editorWorld_.GetEntities()) {
						if (candidate.id == entity->id) continue;
						ImGui::PushID(static_cast<int>(candidate.id));
						if (ImGui::Selectable(candidate.name.c_str(), candidate.id == entity->parent)) {
							changeParent(candidate.id);
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("Position", &entity->transform.position.x, 0.01f);
					ImGui::DragFloat3("Rotation", &entity->transform.rotation.x, 0.01f);
					ImGui::DragFloat3("Scale", &entity->transform.scale.x, 0.01f);
				}
				if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::TextWrapped("GUID: %s", entity->model.assetGuid.empty() ? "legacy path reference" : entity->model.assetGuid.c_str());
					if (entity->hasModel) {
						std::string error;
						Engine::Assets::ResolveModelReference(entity->model, projectAssets_, GetProjectRoot(), error);
						if (!error.empty()) ImGui::TextWrapped("Model reference error: %s", error.c_str());
					}
					if (ImGui::BeginCombo("Registered Model", entity->model.assetPath.c_str())) {
						for (const auto* asset : projectAssets_.Search("", Engine::Assets::AssetType::Model)) {
							const auto source = projectAssets_.ResolveGuid(asset->guid);
							std::error_code ec;
							if (!std::filesystem::is_regular_file(Engine::Assets::AssetRegistry::MetadataPath(source), ec)) continue;
							if (ImGui::Selectable(asset->relativePath.c_str(), entity->model.assetGuid == asset->guid)) {
								const auto relative = std::filesystem::relative(source, GetProjectRoot(), ec);
								const auto hint = (ec ? source : relative).generic_u8string();
								editorWorld_.SetModel(entity->id, std::string(hint.begin(), hint.end()), asset->guid);
							}
						}
						ImGui::EndCombo();
					}
					ImGui::TextWrapped("Open Asset Browser to import/refresh models. Editing the path switches back to a legacy reference.");
					char assetPath[260]{};
					strncpy_s(assetPath, entity->model.assetPath.c_str(), _TRUNCATE);
					if (ImGui::InputText("Model Asset Path", assetPath, sizeof(assetPath))) {
						if (assetPath[0] != '\0') { editorWorld_.SetModel(entity->id, assetPath); }
						else { editorWorld_.RemoveModel(entity->id); }
					}
				}
				if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
					const bool settingsLocked = worldPlayback_.GetState() != WorldPlayback::State::Editing ||
						assetImportJob_.IsBusy() || exportJob_.GetState() == Engine::Services::ExportJob::State::Running;
					ImGui::BeginDisabled(settingsLocked);
					if (!entity->hasAnimator) {
						if (ImGui::Button("Add Animator") && !editorWorld_.SetAnimator(entity->id, {}))
							genericWorldStatus_ = "Animator could not be added";
					} else {
						auto animator = entity->animator;
						char clip[257]{}; strncpy_s(clip, animator.clip.c_str(), _TRUNCATE);
						bool changed = false;
						const auto* selectedController=projectAssets_.FindByGuid(animator.controllerGuid);
						const char* controllerPreview=animator.controllerGuid.empty() ? "None (clip mode)" :
							(selectedController && selectedController->type==Engine::Assets::AssetType::AnimatorController ? selectedController->relativePath.c_str() : "Missing registered Controller");
						const auto selectController=[&](const std::string& guid) {
							Engine::Base::ProjectContext project;std::string error;
							if(!guid.empty() && !project.Open(projectFile_,error)) {genericWorldStatus_=error;return;}
							if(Engine::Editor::PrepareAnimatorControllerSelection(project,projectAssets_,animator,guid,animator,error))changed=true;
							else genericWorldStatus_="Controller selection refused: "+error;
						};
						if(ImGui::BeginCombo("Registered Controller",controllerPreview)) {
							if(ImGui::Selectable("None (clip mode)",animator.controllerGuid.empty()))selectController({});
							for(const auto* asset:projectAssets_.Search("",Engine::Assets::AssetType::AnimatorController)) {
								ImGui::PushID(asset->guid.c_str());const bool selected=animator.controllerGuid==asset->guid;
								if(ImGui::Selectable("##controller",selected))selectController(asset->guid);
								const auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();
								const ImVec4 rowClip(rowMin.x,rowMin.y,rowMax.x,rowMax.y);
								ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),rowMin,
									ImGui::GetColorU32(ImGuiCol_Text),asset->relativePath.data(),asset->relativePath.data()+asset->relativePath.size(),0,&rowClip);
								if(selected)ImGui::SetItemDefaultFocus();ImGui::PopID();
							}
							ImGui::EndCombo();
						}
						const bool controllerMode=!animator.controllerGuid.empty();
						if(controllerMode && ImGui::Button("Open Controller Graph")){std::string error;if(!OpenControllerAssetEditor(animator.controllerGuid,error))genericWorldStatus_=error;}
						if(controllerMode) {ImGui::TextWrapped("Controller GUID: %s",animator.controllerGuid.c_str());ImGui::TextWrapped("Controller states own clips and looping. Animator Speed is a state-speed multiplier.");}
						ImGui::BeginDisabled(controllerMode);
						if (ImGui::InputText("Animator Clip", clip, sizeof(clip))) { animator.clip = clip; changed = true; }
						auto* clipScene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene());
						const auto availableClips=clipScene ? clipScene->GetAnimationClipNames(entity->id) : std::vector<std::string>{};
						ImGui::BeginDisabled(availableClips.empty());
						if(ImGui::BeginCombo("Loaded Model Clips",animator.clip.c_str())) {
							for(const auto& name:availableClips) {
								ImGui::PushID(name.c_str());
								const bool selected=name==animator.clip;
								if(ImGui::Selectable("##loaded_clip",selected)){animator.clip=name;changed=true;}
								if(selected)ImGui::SetItemDefaultFocus();
								const auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();
								const ImVec4 rowClip(rowMin.x,rowMin.y,rowMax.x,rowMax.y);
								ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),rowMin,
									ImGui::GetColorU32(ImGuiCol_Text),name.data(),name.data()+name.size(),0,&rowClip);
								if(ImGui::IsItemHovered()){ImGui::BeginTooltip();ImGui::TextUnformatted(name.c_str());ImGui::EndTooltip();}
								ImGui::PopID();
								}
							ImGui::EndCombo();
						}
						ImGui::EndDisabled();
						if(availableClips.empty())ImGui::TextWrapped("No loaded clip choices: model missing/pending/failed, or no usable authored clips. Manual draft entry remains available.");
						ImGui::EndDisabled();
						changed |= ImGui::DragFloat("Animator Speed", &animator.speed, 0.01f, 0.0f, 1000.0f, "%.3f");
						ImGui::BeginDisabled(controllerMode);
						changed |= ImGui::Checkbox("Animator Loop", &animator.loop);
						ImGui::EndDisabled();
						changed |= ImGui::Checkbox("Animator Play On Awake", &animator.playOnAwake);
						if (changed && !editorWorld_.SetAnimator(entity->id, animator))
							genericWorldStatus_ = "Animator rejected: clip must be nonempty UTF-8 (256 bytes max); speed must be finite and nonnegative";
						if (ImGui::Button("Remove Animator")) editorWorld_.RemoveAnimator(entity->id);
					}
					ImGui::EndDisabled();
					if (entity->hasAnimator) {
						auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene());
						const auto status = scene ? scene->GetAnimatorRenderStatus(entity->id) : EntityScene::AnimatorRenderStatus{};
						if (!entity->hasModel) ImGui::TextWrapped("Animator draft: add a Model to bind an animation clip.");
						else if (!status.ready) ImGui::TextWrapped("Animator not ready: model/clip missing, settings pending, or owner preparation failed. Previous valid owner may remain active.");
						else ImGui::Text("Animator: %s; time %.3f s", status.playing ? "Playing" : "Stopped / paused", status.time);
						if(scene && !entity->animator.controllerGuid.empty()) {
							ImGui::SliderFloat("Force-state blend (seconds)",&controllerDebugBlendSeconds_,0,60,"%.2f",ImGuiSliderFlags_AlwaysClamp);
							for(size_t layer=0;layer<4;++layer) {
								const auto actual=scene->ReadControllerLayer(*entity,entity->animator,layer);if(!actual.ready)continue;
								const std::string current(actual.state);const auto states=scene->GetControllerStateNames(entity->id,layer);
								ImGui::PushID(static_cast<int>(layer));
								ImGui::Text("Layer %u: %s",static_cast<unsigned>(layer),current.c_str());
								ImGui::Text("Time %.3f s; progress %.3f; normalized %.3f",actual.time,actual.progress,actual.normalized);
								ImGui::BeginDisabled(!status.ready || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()==WorldPlayback::State::Editing);
								float weight=actual.weight;
								if(ImGui::SliderFloat("Runtime weight",&weight,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp) && !scene->SetControllerLayerWeight(entity->id,layer,weight))genericWorldStatus_="Controller weight refused: binding unavailable or stale";
								if(ImGui::BeginCombo("Force / restart state",current.c_str())) {
									for(size_t stateIndex=0;stateIndex<states.size();++stateIndex) {
										const auto& name=states[stateIndex];ImGui::PushID(static_cast<int>(stateIndex));
										if(ImGui::Selectable("##force_state",name==current) && !scene->ForceControllerState(entity->id,layer,name,controllerDebugBlendSeconds_))genericWorldStatus_="Controller force-state refused: binding/clip/pose unavailable or stale";
										const auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();const ImVec4 rowClip(rowMin.x,rowMin.y,rowMax.x,rowMax.y);
										ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),rowMin,ImGui::GetColorU32(ImGuiCol_Text),name.data(),name.data()+name.size(),0,&rowClip);
										ImGui::PopID();
									}
									ImGui::EndCombo();
								}
								ImGui::EndDisabled();ImGui::PopID();
								}
							ImGui::TextWrapped("Layer controls change runtime only, not Scene or Controller assets. Force-state preserves parameters/triggers; selecting the same state restarts it. Zero blend switches immediately.");
						}
						ImGui::BeginDisabled(!status.ready || !scene || worldPlayback_.GetState() == WorldPlayback::State::Editing);
						const auto command = [&](const char* label, Engine::AnimationSystem::AnimatorCommand action) {
							if (ImGui::Button(label) && !scene->ControlAnimator(entity->id, action)) genericWorldStatus_ = "Animator command refused: binding is unavailable or stale";
						};
						command("Animator Play", Engine::AnimationSystem::AnimatorCommand::Play); ImGui::SameLine();
						command("Animator Pause", Engine::AnimationSystem::AnimatorCommand::Pause);
						command("Animator Stop", Engine::AnimationSystem::AnimatorCommand::Stop); ImGui::SameLine();
						command("Animator Restart", Engine::AnimationSystem::AnimatorCommand::Restart);
						ImGui::EndDisabled();
						ImGui::TextWrapped("Settings use Scene Undo/Redo. Playback buttons change runtime only; Scene Pause freezes all animation clocks.");
					}
				}
				if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (!entity->hasCamera) {
						if (ImGui::Button("Add Camera")) editorWorld_.SetCamera(entity->id, {});
					} else {
						auto camera = entity->camera;
						bool changed = ImGui::Checkbox("Primary Camera", &camera.primary);
						int projection = camera.orthographic ? 1 : 0;
						if (ImGui::Combo("Projection", &projection, "Perspective\0Orthographic\0")) {
							camera.orthographic = projection == 1; changed = true;
						}
						if (camera.orthographic)
							changed |= ImGui::DragFloat("Vertical Height (world units)", &camera.orthographicHeight, 0.1f, 0.001f, 100000.0f);
						else changed |= ImGui::DragFloat("FOV Y (radians)", &camera.fovY, 0.01f, 0.01f, 3.13f);
						changed |= ImGui::DragFloat("Near Clip", &camera.nearClip, 0.01f, 0.001f, 10000.0f);
						changed |= ImGui::DragFloat("Far Clip", &camera.farClip, 0.1f, 0.002f, 100000.0f);
						if (changed && !editorWorld_.SetCamera(entity->id, camera))
							genericWorldStatus_ = "Camera rejected: require finite positive height, 0 < FOV < pi and 0 < near < far";
						ImGui::TextWrapped("Multiple primary cameras: lowest active entity ID wins. No valid camera: default view.");
						if (ImGui::Button("Remove Camera")) editorWorld_.RemoveCamera(entity->id);
					}
				}
				if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::BeginDisabled(SceneEditUnavailableReason(Action::Duplicate) != nullptr || projectFile_.empty());
					if (ImGui::Button("New Lua Script...")) {
						wchar_t filename[32768]=L"Trigger.lua";
						OPENFILENAMEW dialog{};
						dialog.lStructSize=sizeof(dialog);
						dialog.hwndOwner=winApp->GetHwnd();
						dialog.lpstrFilter=L"Lua scripts\0*.lua\0";
						dialog.lpstrFile=filename;
						dialog.nMaxFile=static_cast<DWORD>(std::size(filename));
						dialog.lpstrInitialDir=projectAssetRoot_.c_str();
						dialog.lpstrDefExt=L"lua";
						dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
						if (GetSaveFileNameW(&dialog)) {
							std::string error;
							if (CreateLuaScriptAsset(filename,error))
								genericWorldStatus_="Lua script created. Select it under Registered Script after import.";
							else genericWorldStatus_="Lua creation: " + error;
						}
					}
					ImGui::EndDisabled();
					ImGui::TextWrapped("GUID: %s", entity->script.assetGuid.empty() ? "legacy path reference" : entity->script.assetGuid.c_str());
					if (ImGui::BeginCombo("Registered Script", entity->script.assetPath.c_str())) {
						for (const auto* asset : projectAssets_.Search("", Engine::Assets::AssetType::Script)) {
							std::error_code ec;
							const auto source = projectAssets_.ResolveGuid(asset->guid);
							if (!std::filesystem::is_regular_file(Engine::Assets::AssetRegistry::MetadataPath(source), ec)) continue;
							if (ImGui::Selectable(asset->relativePath.c_str(), entity->script.assetGuid == asset->guid)) {
								const auto relative = std::filesystem::relative(source, GetProjectRoot(), ec);
								const auto hint = (ec ? source : relative).generic_u8string();
								editorWorld_.SetScript(entity->id, std::string(hint.begin(), hint.end()), entity->script.enabled, asset->guid);
							}
						}
						ImGui::EndCombo();
					}
					if (!entity->hasScript) {
						if (ImGui::Button("Add Script")) editorWorld_.SetScript(entity->id, "Scripts/new.kubo");
					} else {
						char scriptPath[260]{};
						strncpy_s(scriptPath, entity->script.assetPath.c_str(), _TRUNCATE);
						if (ImGui::InputText("Script Asset Path", scriptPath, sizeof(scriptPath))) {
							if (scriptPath[0] != '\0') editorWorld_.SetScript(entity->id, scriptPath, entity->script.enabled);
							else editorWorld_.RemoveScript(entity->id);
						}
						ImGui::Checkbox("Script Enabled", &entity->script.enabled);
						const auto runtimeError=scriptRuntime_.GetLastError(entity->id);
						if(!runtimeError.empty()) ImGui::TextWrapped("Script suspended: %s",runtimeError.c_str());
						const auto loadError=scriptRuntime_.GetAssetLoadError(entity->script.RegistrationKey());
						if(!loadError.empty()) ImGui::TextWrapped("Last source load failed (an older registration may still run): %s",loadError.c_str());
						if (ImGui::Button("Load Script Asset")) {
							std::string error;
							genericWorldStatus_ = Engine::Scripting::LoadScriptReference(scriptRuntime_, entity->script, GetProjectRoot(), projectAssets_, error, true)
								? "Script loaded: " + entity->script.assetPath
								: "Script load failed: " + error;
						}
						const auto defaults=scriptRuntime_.GetPropertyDefaults(entity->script.RegistrationKey());
						if(defaults.empty()) ImGui::TextWrapped("Load a Lua asset declaring properties to edit public values.");
						for(const auto& [name,defaultValue]:defaults) {
							ImGui::PushID(name.c_str());ImGui::TextUnformatted(name.c_str());
							const auto overrideValue=entity->script.properties.find(name);
							const bool overridden=overrideValue!=entity->script.properties.end();
							const bool typeMismatch=overridden && overrideValue->second.index()!=defaultValue.index();
							auto property=overridden && overrideValue->second.index()==defaultValue.index() ? overrideValue->second : defaultValue;
							bool changed=false;
							if(auto* number=std::get_if<double>(&property)) changed=ImGui::InputDouble("##value",number,0.1,1.0,"%.6g");
							else if(auto* boolean=std::get_if<bool>(&property)) changed=ImGui::Checkbox("##value",boolean);
							else {char text[1025]{};strncpy_s(text,std::get<std::string>(property).c_str(),_TRUNCATE);if(ImGui::InputText("##value",text,sizeof(text))) {property=std::string(text);changed=true;}}
							if(changed && !editorWorld_.SetScriptProperty(entity->id,name,std::move(property))) genericWorldStatus_="Script property rejected: invalid value or size";
							ImGui::SameLine();if(ImGui::Button("Reset") && overridden) editorWorld_.RemoveScriptProperty(entity->id,name);
							if(typeMismatch) ImGui::TextUnformatted("Override type changed; edit or Reset to recover.");
							ImGui::PopID();
						}
						const auto authoredProperties=entity->script.properties;
						for(const auto& [name,value]:authoredProperties) if(!defaults.contains(name)) {
							(void)value;ImGui::PushID(name.c_str());ImGui::TextWrapped("Unused override: %s",name.c_str());ImGui::SameLine();
							if(ImGui::Button("Remove")) editorWorld_.RemoveScriptProperty(entity->id,name);ImGui::PopID();
						}
					}
				}
                ImGui::BeginDisabled(assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running);
                Engine::Editor::DrawLightInspector(editorWorld_,entity->id,genericWorldStatus_);
                Engine::Editor::DrawMaterialInspector(editorWorld_,entity->id,genericWorldStatus_);
                Engine::Editor::DrawCanvasInspector(editorWorld_,entity->id,genericWorldStatus_);
                Engine::Editor::DrawSpriteInspector(editorWorld_,entity->id,projectAssets_,GetProjectRoot(),genericWorldStatus_);
                Engine::Editor::DrawParticleInspector(editorWorld_,entity->id,projectAssets_,GetProjectRoot(),genericWorldStatus_);
                ImGui::EndDisabled();
                if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())){
                    ImGui::Text("Light frame: %u / 64; excluded invalid branches: %zu",scene->GetLightingFrame().gpu.count,scene->GetLightingFrame().skippedInvalidFrames);
                    if(scene->GetLightingResult()!=RenderLightResult::Success)ImGui::TextWrapped("Light frame failed validation/capacity/work/allocation; previous complete frame retained. See Console.");
                    ImGui::Text("Sprite frame: %zu / 1024; no Canvas: %zu; unassigned: %zu; invalid branches: %zu",scene->GetSpriteFrame().items.size(),scene->GetSpriteFrame().withoutCanvas,scene->GetSpriteFrame().unassigned,scene->GetSpriteFrame().skippedInvalidFrames);
                    if(scene->GetSpriteResult()!=RenderSpriteResult::Success)ImGui::TextWrapped("Sprite frame rejected; no stale/prefix UI is drawn. See Console.");
                }
				if (ImGui::CollapsingHeader("Audio Source",ImGuiTreeNodeFlags_DefaultOpen)) {
					if(!entity->hasAudio) {
						if(ImGui::Button("Add Audio Source")) editorWorld_.SetAudio(entity->id,{});
					} else {
						auto audio=entity->audio;
						bool changed=ImGui::Checkbox("Audio Enabled",&audio.enabled);
						changed|=ImGui::Checkbox("Play On Awake",&audio.playOnAwake);
						changed|=ImGui::Checkbox("Audio Loop",&audio.loop);
						changed|=ImGui::SliderFloat("Audio Volume",&audio.volume,0,1);
						char path[4097]{};strncpy_s(path,audio.assetPath.c_str(),_TRUNCATE);
						if(ImGui::InputText("Audio Asset Path",path,sizeof(path))) {audio.assetPath=path;audio.assetGuid.clear();changed=true;}
						if(ImGui::BeginCombo("Registered Wave",audio.assetPath.c_str())) {
							for(const auto* asset:projectAssets_.Search("",Engine::Assets::AssetType::Audio)) {
								const auto source=projectAssets_.ResolveGuid(asset->guid);auto extension=source.extension().wstring();
								for(auto& c:extension) if(c>=L'A' && c<=L'Z') c+=L'a'-L'A';
								std::error_code ec;
								if(extension!=L".wav" || !std::filesystem::is_regular_file(Engine::Assets::AssetRegistry::MetadataPath(source),ec)) continue;
								if(ImGui::Selectable(asset->relativePath.c_str(),audio.assetGuid==asset->guid)) {
									audio.assetPath=Engine::Base::PathToUtf8(source.lexically_relative(GetProjectRoot()));audio.assetGuid=asset->guid;changed=true;
								}
							}
							ImGui::EndCombo();
						}
						if(changed && !editorWorld_.SetAudio(entity->id,audio)) genericWorldStatus_="Audio source settings/reference rejected";
						const auto error=entityAudio_.GetLastError(entity->id);if(!error.empty()) ImGui::TextWrapped("AudioSource: %s",error.c_str());
						ImGui::TextWrapped("GUID: %s",entity->audio.assetGuid.empty()?"legacy path reference":entity->audio.assetGuid.c_str());
						if(ImGui::Button("Remove Audio Source")) editorWorld_.RemoveAudio(entity->id);
					}
				}
				if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (!entity->hasCollider) {
						if (ImGui::Button("Add Collider")) editorWorld_.SetCollider(entity->id, {{0.5f, 0.5f, 0.5f}, false});
					} else {
						bool enabled = true;
						if (ImGui::Checkbox("Collider Enabled", &enabled) && !enabled) editorWorld_.RemoveCollider(entity->id);
						if (entity->hasCollider) {
							auto& collider=entity->collider;
							int shape=static_cast<int>(collider.shape);
							if (ImGui::Combo("Shape",&shape,"Box\0Sphere\0Capsule\0"))
								collider.shape=static_cast<Physics::ColliderShape>(shape);
							if (collider.shape==Physics::ColliderShape::Box)
								ImGui::DragFloat3("Half Extents",&collider.halfExtents.x,0.01f,0,100,"%.3f",ImGuiSliderFlags_AlwaysClamp);
							else
								ImGui::DragFloat("Radius",&collider.radius,0.01f,0.001f,100,"%.3f",ImGuiSliderFlags_AlwaysClamp);
							if (collider.shape==Physics::ColliderShape::Capsule)
								ImGui::DragFloat("Half Segment (local Y)",&collider.halfSegment,0.01f,0,100,"%.3f",ImGuiSliderFlags_AlwaysClamp);
							ImGui::Checkbox("Is Trigger", &entity->collider.isTrigger);
							ImGui::SliderFloat("Friction", &entity->collider.friction, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
							ImGui::SliderFloat("Restitution", &entity->collider.restitution, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
							ImGui::TextWrapped("Friction uses both colliders; restitution bounces above 0.5 units/s. Triggers report overlap.");
						}
					}
				}
				if (ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (!entity->hasRigidBody) {
						if (ImGui::Button("Add RigidBody")) editorWorld_.SetRigidBody(entity->id, {});
					} else {
						bool enabled = true;
						if (ImGui::Checkbox("RigidBody Enabled", &enabled) && !enabled) editorWorld_.RemoveRigidBody(entity->id);
						if (entity->hasRigidBody) {
							ImGui::Checkbox("Dynamic", &entity->rigidBody.dynamic);
							ImGui::Checkbox("Use Gravity", &entity->rigidBody.useGravity);
							ImGui::DragFloat("Mass", &entity->rigidBody.mass, 0.01f, 0.01f, 1000.0f);
							ImGui::DragFloat3("World Velocity", &entity->rigidBody.velocity.x, 0.01f);
						}
					}
				}
				if (ImGui::Button("Delete primary Entity (root children)") && editorWorld_.DestroyEntity(entity->id)) {
					selectedEditorEntity_ = 0;
				}
			}
			ImGui::EndDisabled();
		}
		ImGui::End();
	}
	if (!prefabRecoveryBlocked_ && worldPlayback_.GetState() == WorldPlayback::State::Editing) {
		std::string error;
		editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
		if (!entityHistory_.Record(beforeEdit, editorWorld_, beforeSelection, editorSelection_.Capture(), error)) {
			std::string restoreError;
			if (editorWorld_.Restore(beforeEdit, restoreError) && editorSelection_.Restore(editorWorld_,beforeSelection,restoreError)) selectedEditorEntity_ = editorSelection_.Primary();
			genericWorldStatus_ = "Edit rejected: " + error;
		}
		if ((!ImGui::IsAnyItemActive() && !gizmoDragActive_) || documentState_.HasPendingAction()) entityHistory_.Commit();
		documentState_.Observe(editorWorld_);
	}
	ImGui::EndDisabled();
	SynchronizeGenericPlayer();
#endif // _DEBUG
}

bool Game::SaveGenericDocument()
{
	{std::string error;if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}}
	if (worldPlayback_.GetState() != WorldPlayback::State::Editing) return false;
	if(genericWorldPath_.empty()) return BrowseSaveSceneAs();
	entityHistory_.Commit();
	std::string error;
	EntityWorld checked;
	if (!checked.Restore(editorWorld_.Capture(), error) || !editorWorld_.Save(genericWorldPath_, error)) {
		genericWorldStatus_ = "Save failed: " + error;
		return false;
	}
	documentState_.MarkSaved(editorWorld_);
	hubPreviewRequested_ = true;
	genericWorldStatus_ = "Saved: " + Engine::Base::PathToUtf8(genericWorldPath_);
	return true;
}

bool Game::ReadPendingPrefabs(std::vector<Engine::Editor::PrefabPendingEntry>& entries,std::string& error) const
{
	if(!editorMode_ || projectFile_.empty()){entries.clear();error.clear();return true;}
	Engine::Base::ProjectContext project;
	if(!project.Open(projectFile_,error))return false;
	if(project.GetRootPath()!=GetProjectRoot() || (!projectAssetRoot_.empty() && project.ResolvePath(project.GetSettings().assetDirectory)!=projectAssetRoot_)){
		error="Project/asset root changed; reopen the project before authoring";return false;
	}
	return Engine::Editor::PrefabPendingTransactions::Scan(project,entries,error);
}
bool Game::CheckPrefabRecovery(std::string& error) const
{
	std::vector<Engine::Editor::PrefabPendingEntry> entries;
	if(!ReadPendingPrefabs(entries,error))return false;
	if(!entries.empty()){error="Unresolved Prefab source transaction; inspect Prefab Recovery before editing/saving/exporting";return false;}
	error.clear();return true;
}
bool Game::RefreshPrefabRecovery(std::string& error)
{
	std::vector<Engine::Editor::PrefabPendingEntry> entries;
	const bool read=ReadPendingPrefabs(entries,error);const bool wasBlocked=prefabRecoveryBlocked_;
	prefabRecoveryBlocked_=!read || !entries.empty();
	if(read)prefabPending_=std::move(entries);
	if(!read)prefabRecoveryStatus_="Pending scan failed: "+error;
	if(prefabRecoveryBlocked_ && !wasBlocked){
		prefabRecoveryWindowOpen_=true;
		assetImportJob_.CancelAndWait();
		if(exportJob_.GetState()==Engine::Services::ExportJob::State::Running){std::string status;exportJob_.Cancel(status);}
		if(read)prefabRecoveryStatus_="Unresolved Prefab data detected. No data is deleted automatically.";
		if(gizmoDragActive_){entityHistory_.Commit();gizmoDragActive_=false;groupGizmoPivotValid_=false;}
	}
	return read;
}
bool Game::RefreshHistoryRecovery(std::string& error) noexcept
{
	try {return RefreshPrefabRecovery(error);}
	catch(...) {
		// A replay may already be committed. Keep the guard even if inventory
		// allocation/reporting fails; never turn that success into a rollback.
		prefabRecoveryBlocked_=true;prefabRecoveryWindowOpen_=true;
		try {error="Prefab pending inventory refresh failed; authoring is blocked";}catch(...){}
		return false;
	}
}
bool Game::CanRecoverPrefab(std::string& error) const
{
	if(!editorMode_ || projectFile_.empty() || worldPlayback_.GetState()!=WorldPlayback::State::Editing ||
		gizmoDragActive_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running){
		error="Recovery requires an idle Editor in Editing mode; stop playback and wait for jobs/document actions";return false;
	}
	return true;
}
bool Game::InspectEditorPrefabRecovery(const std::string& guid,std::string& error)
{
	prefabRecoveryInspected_=false;
	if(!CanRecoverPrefab(error) || !RefreshPrefabRecovery(error))return false;
	const auto found=std::find_if(prefabPending_.begin(),prefabPending_.end(),[&](const auto& item){return item.guid==guid;});
	if(found==prefabPending_.end() || (found->kind!=Engine::Editor::PrefabPendingKind::History && found->kind!=Engine::Editor::PrefabPendingKind::Publication)){
		error="Incomplete/unknown Prefab data must be preserved for manual recovery";return false;
	}
	Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))return false;
	prefabRecoveryHistory_=found->kind==Engine::Editor::PrefabPendingKind::History;
	const bool inspected=prefabRecoveryHistory_?Engine::Editor::PrefabSourceHistoryRecovery::Inspect(project,guid,prefabHistoryRecovery_,error):
		Engine::Editor::PrefabSourceRecovery::Inspect(project,guid,prefabSourceRecovery_,error);
	prefabRecoveryInspected_=inspected;prefabRecoveryStatus_=inspected?"Inspected. Physical source state is not proof of Scene/history publication.":error;
	return inspected;
}
bool Game::RestoreEditorPrefabRecovery(std::string& error)
{
	if(!CanRecoverPrefab(error))return false;
	if(!prefabRecoveryInspected_){error="Inspect the selected pending transaction first";return false;}
	Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))return false;
	const auto guid=prefabRecoveryHistory_?prefabHistoryRecovery_.source.guid:prefabSourceRecovery_.guid;
	const bool restored=prefabRecoveryHistory_?Engine::Editor::PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,guid,prefabHistoryRecovery_,error):
		Engine::Editor::PrefabSourceRecovery::RestoreOriginal(project,guid,prefabSourceRecovery_,error);
	if(!restored){prefabRecoveryStatus_=error;return false;}
	if(!InspectEditorPrefabRecovery(guid,error)){prefabRecoveryStatus_="Source restored, but reinspection failed: "+error;return false;}
	prefabRecoveryStatus_="Source restored; marker remains. Scene and selection are unchanged. Complete recovery explicitly to resume.";return true;
}
bool Game::CompleteEditorPrefabRecovery(std::string& error)
{
	if(!CanRecoverPrefab(error))return false;
	if(!prefabRecoveryInspected_ || documentState_.IsDirty()){
		error="Inspect restored data first. Unsaved Scene: close/discard and reopen before completion; no automatic Scene merge is performed.";return false;
	}
	if(genericSceneSelected_){
		EntityWorld saved;
		if(documentState_.Path().empty() || !saved.Load(documentState_.Path(),error) || saved.Capture().dump()!=editorWorld_.Capture().dump()){
			error="Saved Scene changed/unreadable or differs from the current clean Scene; reopen before recovery completion";return false;
		}
	}
	Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))return false;
	std::filesystem::path archived;
	const auto guid=prefabRecoveryHistory_?prefabHistoryRecovery_.source.guid:prefabSourceRecovery_.guid;
	if(!prefabRecoveryHistory_ && prefabSourceRecovery_.state!=Engine::Editor::PrefabSourceRecoveryState::Prepared){error="Restore the original source before completing publication recovery";return false;}
	const bool completed=prefabRecoveryHistory_?Engine::Editor::PrefabSourceHistoryRecovery::ArchiveRestored(project,guid,prefabHistoryRecovery_,archived,error):
		Engine::Editor::PrefabSourceAcknowledgement::ArchiveResolved(project,guid,prefabSourceRecovery_,archived,error);
	if(!completed){prefabRecoveryStatus_=error;return false;}
	entityHistory_.Clear();prefabRecoveryInspected_=false;physicsWorld_.ResetSimulation();
	if(!RefreshPrefabRecovery(error)){prefabRecoveryStatus_="Archived, but pending rescan failed: "+error;return false;}
	prefabRecoveryStatus_="Retained recovery data: "+Engine::Base::PathToUtf8(archived)+". Current Scene/selection kept; stale Undo/Redo cleared.";
	error.clear();return true;
}
void Game::DrawPrefabRecoveryWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(!prefabRecoveryWindowOpen_)return;
	ImGui::SetNextWindowSize({680,440},ImGuiCond_FirstUseEver);
	if(ImGui::Begin("Prefab Recovery",&prefabRecoveryWindowOpen_)){
		ImGui::TextWrapped("Pending source changes block authoring, saving, playback, asset writes, MCP edits and export. Inspection never deletes data. Closing this window does not release the guard.");
		if(ImGui::Button("Refresh pending inventory")){std::string error;RefreshPrefabRecovery(error);}
		if(worldPlayback_.GetState()!=WorldPlayback::State::Editing && ImGui::Button("Stop playback for recovery")){
			std::string error;if(StopEditorPlayback(error)){
				selectedEditorEntity_=editorWorld_.Find(authoredSelection_)?authoredSelection_:0;editorSelection_.Replace(selectedEditorEntity_);
				if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))scene->ResetAnimations();
			}else prefabRecoveryStatus_=error;
		}
		std::string selectedGuid;
		for(const auto& entry:prefabPending_){
			const auto label=entry.name+"##pending-prefab-"+entry.name;
			ImGui::BeginDisabled(entry.guid.empty());if(ImGui::Selectable(label.c_str()))selectedGuid=entry.guid;ImGui::EndDisabled();
		}
		if(!selectedGuid.empty()){std::string error;if(!InspectEditorPrefabRecovery(selectedGuid,error))prefabRecoveryStatus_=error;}
		if(prefabRecoveryInspected_){
			const auto& report=prefabRecoveryHistory_?prefabHistoryRecovery_.source:prefabSourceRecovery_;
			const auto state=report.state==Engine::Editor::PrefabSourceRecoveryState::Prepared?"Old source active":report.state==Engine::Editor::PrefabSourceRecoveryState::Parked?"Both sources parked":report.state==Engine::Editor::PrefabSourceRecoveryState::Published?"New source active":"Conflict; automatic restore refused";
			ImGui::Text("GUID: %s",report.guid.c_str());ImGui::TextWrapped("Source: %s",Engine::Base::PathToUtf8(report.source).c_str());ImGui::TextUnformatted(state);
			if(prefabRecoveryHistory_)ImGui::Text("Interrupted direction: %s",prefabHistoryRecovery_.undo?"Undo":"Redo");
			if(ImGui::Button("Restore pre-operation source..."))ImGui::OpenPopup("Confirm Prefab source restore");
			ImGui::SameLine();if(ImGui::Button("Complete recovery..."))ImGui::OpenPopup("Confirm Prefab recovery completion");
		}
		if(ImGui::BeginPopupModal("Confirm Prefab source restore",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
			ImGui::TextWrapped("Restore the source active before the interrupted operation. Current Scene, selection and document stay unchanged. Pending and both source versions are retained; editing remains blocked.");
			if(ImGui::Button("Restore source")){std::string error;if(!RestoreEditorPrefabRecovery(error))prefabRecoveryStatus_=error;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel restore"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Confirm Prefab recovery completion",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
			ImGui::TextWrapped("Keep the current unchanged Scene. Archive all pending data without deletion and discard stale in-memory Undo/Redo. No closed Scene is rewritten and no Prefab propagation is performed. Completion requires a clean Scene and a revalidated restored source.");
			if(ImGui::Button("Keep Scene and archive pending")){std::string error;if(!CompleteEditorPrefabRecovery(error))prefabRecoveryStatus_=error;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel completion"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
		}
		ImGui::TextWrapped("%s",prefabRecoveryStatus_.c_str());
	}
	ImGui::End();
#endif
}

bool Game::CanAuthorPrefab(std::string& error) const
{
	if(!CheckPrefabRecovery(error))return false;
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Prefab authoring requires an idle generic Scene in Editing mode";return false;
	}
	error.clear();return true;
}
bool Game::OpenPrefabProject(Engine::Base::ProjectContext& project,std::string& error) const
{
	if(!project.Open(projectFile_,error))return false;
	if(project.GetRootPath()!=GetProjectRoot()){error="Prefab authoring cannot switch the active project root";return false;}
	if(project.ResolvePath(project.GetSettings().assetDirectory)!=projectAssetRoot_){error="Prefab authoring cannot switch the active asset root; reopen the project";return false;}
	return true;
}
bool Game::SaveEditorPrefab(const std::filesystem::path& path,std::filesystem::path& saved,std::string& error)
{
	Engine::Base::ProjectContext project;
	if(!CanAuthorPrefab(error) || !OpenPrefabProject(project,error) ||
		!Engine::Editor::SaveProjectPrefab(project,editorWorld_,selectedEditorEntity_,path,saved,error)) {
		genericWorldStatus_="Prefab save rejected: "+error;return false;
	}
	genericWorldStatus_="Saved new Prefab: "+Engine::Base::PathToUtf8(saved)+"; refresh Asset Browser to register GUID";return true;
}
bool Game::InstantiateEditorPrefab(const std::string& guid,EntityId parent,EntityId& created,std::string& error,
	std::optional<Vector3> placement)
{
	Engine::Base::ProjectContext project;Engine::Editor::PrefabValue source;
	if(!CanAuthorPrefab(error) || !OpenPrefabProject(project,error) || !Engine::Editor::ReadProjectPrefab(project,projectAssets_,guid,source,error)) {
		genericWorldStatus_="Prefab placement rejected: "+error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::InstantiatePrefab(editorWorld_,source,guid,parent,editorSelection_,entityHistory_,created,error,placement)) {
		genericWorldStatus_="Prefab placement rejected: "+error;return false;
	}
	selectedEditorEntity_=editorSelection_.Primary();documentState_.Observe(editorWorld_);physicsWorld_.ResetSimulation();
	genericWorldStatus_=placement ? "Placed linked Prefab at cursor" : "Placed linked Prefab; source local TRS retained";return true;
}
void Game::CancelEditorPrefabRevert() noexcept
{
	pendingPrefabRevertRoot_=0;pendingPrefabRevertGeneration_=0;pendingPrefabRevertWorld_.clear();pendingPrefabRevertScene_.clear();
}
bool Game::RequestEditorPrefabRevert(EntityId root,std::string& error)
{
	try {
		if(HasPendingPrefabRevert() || prefabApply_ || prefabFieldRevert_)throw std::runtime_error("A Prefab confirmation is already pending");
		if(!CanAuthorPrefab(error))throw std::runtime_error(error);
		const auto* entity=editorWorld_.Find(root);if(!entity || entity->prefabLink.is_null())throw std::runtime_error("Revert requires a linked Prefab root");
		auto world=editorWorld_.Capture().dump();auto scene=documentState_.Path();
		pendingPrefabRevertWorld_.swap(world);pendingPrefabRevertScene_.swap(scene);
		pendingPrefabRevertGeneration_=documentState_.Generation();pendingPrefabRevertRoot_=root;error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::ConfirmEditorPrefabRevert(std::string& error)
{
	const auto root=pendingPrefabRevertRoot_;const auto generation=pendingPrefabRevertGeneration_;
	std::string world;world.swap(pendingPrefabRevertWorld_);std::filesystem::path scene;scene.swap(pendingPrefabRevertScene_);CancelEditorPrefabRevert();
	try {
		if(!root)throw std::runtime_error("No Prefab Revert confirmation is pending");
		if(generation!=documentState_.Generation() || scene!=documentState_.Path() || world!=editorWorld_.Capture().dump())throw std::runtime_error("Scene session or World changed; request confirmation again");
		return RevertEditorPrefab(root,error);
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::RevertEditorPrefab(EntityId root,std::string& error)
{
	bool committed=false;
	try {
	if(!CanAuthorPrefab(error)){genericWorldStatus_="Prefab Revert rejected: "+error;return false;}
	const auto* entity=editorWorld_.Find(root);
	if(!entity || entity->prefabLink.is_null()) {
		error="Revert requires a linked Prefab root";genericWorldStatus_="Prefab Revert rejected: "+error;return false;
	}
	// Read identity from a validated snapshot, not unchecked mutable link fields.
	EntityWorld checked;
	if(!checked.Restore(editorWorld_.Capture(),error)){genericWorldStatus_="Prefab Revert rejected: "+error;return false;}
	const auto guid=checked.Find(root)->prefabLink["sourceGuid"].get<crude_json::string>();
	Engine::Base::ProjectContext project;Engine::Editor::HeldProjectPrefabRevision read;
	if(!OpenPrefabProject(project,error) || !read.Open(project,projectAssets_,guid,error)) {
		genericWorldStatus_="Prefab Revert rejected: "+error;return false;
	}
	auto nextSelection=editorSelection_;auto nextPrimary=selectedEditorEntity_;nextSelection.Synchronize(editorWorld_,nextPrimary);
	if(!Engine::Editor::RevertPrefabInstance(editorWorld_,root,read.Snapshot().definition,guid,nextSelection,entityHistory_,error,&documentState_)) {
		genericWorldStatus_="Prefab Revert rejected: "+error;return false;
	}
	// read retains BOTH source/metadata native handles through authored publication.
	editorSelection_=std::move(nextSelection);selectedEditorEntity_=editorSelection_.Primary();committed=true;physicsWorld_.ResetSimulation();
	if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))scene->ResetAnimations();
	genericWorldStatus_="Reverted Prefab to current source (including root local TRS); Undo available";return true;
	}catch(const std::exception& exception){if(committed){error.clear();return true;}error=exception.what();genericWorldStatus_="Prefab Revert rejected: "+error;return false;}
}
bool Game::RequestEditorPrefabApply(EntityId root,std::string& error)
{
	return RequestEditorPrefabApplyInternal(root,nullptr,error);
}
bool Game::RequestEditorPrefabFieldApply(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,std::string& error)
{
	return RequestEditorPrefabApplyInternal(root,&entry,error);
}
bool Game::RequestEditorPrefabApplyInternal(EntityId root,const Engine::Editor::PrefabOverrideEntry* entry,std::string& error)
{
	try {
		if(HasPendingPrefabRevert() || prefabApply_ || prefabFieldRevert_){error="A Prefab confirmation is already pending";return false;}
		if(!CanAuthorPrefab(error))throw std::runtime_error(error);
		EntityWorld checked;if(!checked.Restore(editorWorld_.Capture(),error))throw std::runtime_error(error);
		const auto* entity=checked.Find(root);if(!entity || entity->prefabLink.is_null())throw std::runtime_error("Apply requires a linked Prefab root");
		const auto guid=entity->prefabLink["sourceGuid"].get<crude_json::string>();
		Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))throw std::runtime_error(error);
		Engine::Editor::HeldProjectPrefabRevision read;if(!read.Open(project,projectAssets_,guid,error))throw std::runtime_error(error);
		Engine::Editor::PrefabApplyPlan plan;
		if(entry) {
			std::unique_ptr<Engine::Editor::PrefabApplyPlan> fieldPlan;
			if(!Engine::Editor::BuildPrefabFieldApplyPlan(editorWorld_,root,read.Snapshot().definition,guid,*entry,fieldPlan,error))throw std::runtime_error(error);
			plan=std::move(*fieldPlan);
		}else if(!Engine::Editor::BuildPrefabApplyPlan(editorWorld_,root,read.Snapshot().definition,guid,plan,error))throw std::runtime_error(error);
		auto confirmation=std::make_unique<PrefabApplyConfirmation>();confirmation->root=root;confirmation->scene=documentState_.Path();confirmation->revision=read.Snapshot();
		if(!entry && !Engine::Editor::BuildPrefabApplySourceReview(editorWorld_,root,confirmation->revision.definition,guid,plan.source,confirmation->sourceReview,error))throw std::runtime_error(error);
		confirmation->primary=selectedEditorEntity_;
		if(entry) {
			confirmation->field=*entry;
			std::vector<std::string> tokens;if(!Engine::Editor::PrefabOverrideTokens(entry->path,tokens,error))throw std::runtime_error(error);
			for(const auto& node:confirmation->revision.definition["world"]["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(entry->local))
				if(const auto* value=Engine::Editor::PrefabOverrideValue(node,tokens))confirmation->sourceValue=value->dump();
			confirmation->comparison=Engine::Editor::ClassifyPrefabThreeWayValues(entry->baseline,confirmation->sourceValue,entry->instance);
		}
		if(plan.source.dump()!=confirmation->revision.definition.dump())confirmation->effect=std::make_shared<Engine::Editor::NativePrefabApplyEffect>(project,confirmation->revision,plan.source);
		confirmation->instances=std::move(plan.instances);
		const bool prepared=entry?
			confirmation->admission.PrepareField(editorWorld_,root,confirmation->revision.definition,guid,*entry,editorSelection_,entityHistory_,confirmation->effect,error,&documentState_):
			confirmation->admission.Prepare(editorWorld_,root,confirmation->revision.definition,guid,editorSelection_,entityHistory_,confirmation->effect,error,&documentState_);
		if(!prepared)throw std::runtime_error(error);
		prefabApply_=std::move(confirmation);genericWorldStatus_="Apply preview ready; no source or Scene file has been written";error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();genericWorldStatus_="Prefab Apply preview rejected: "+error;return false;}
}
bool Game::ConfirmEditorPrefabApply(std::string& error)
{
	return ConfirmEditorPrefabApply(error,false);
}
bool Game::ConfirmEditorPrefabApply(std::string& error,bool approveFieldConflict)
{
	auto confirmation=std::move(prefabApply_);bool committed=false;
	try {
		if(!confirmation)throw std::runtime_error("No Prefab Apply confirmation is pending");
		if(confirmation->field && confirmation->comparison==Engine::Editor::PrefabThreeWayChange::Conflict && !approveFieldConflict)
			throw std::runtime_error("Field Apply conflict requires explicit approval to replace the current source value");
		if(!confirmation->field && confirmation->sourceReview.RequiresApproval() && !approveFieldConflict)
			throw std::runtime_error("Whole Apply requires explicit approval to replace reviewed current-source changes");
		if(!CanAuthorPrefab(error))throw std::runtime_error(error);
		if(confirmation->scene!=documentState_.Path())throw std::runtime_error("Apply Scene changed; request confirmation again");
		if(confirmation->field && confirmation->primary!=selectedEditorEntity_)throw std::runtime_error("Field Apply primary changed; request confirmation again");
		Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))throw std::runtime_error(error);
		Engine::Editor::HeldProjectPrefabRevision read;
		if(!read.Open(project,projectAssets_,confirmation->revision.guid,error,&confirmation->revision))throw std::runtime_error(error);
		// Initial native publication reacquires DELETE ownership and exact revision.
		// Scene-only/no-op keeps this read lease through memory/document publication.
		if(confirmation->effect)read.Release();
		if(!confirmation->admission.Publish(editorWorld_,editorSelection_,entityHistory_,error,&documentState_))throw std::runtime_error(error);
		committed=true;selectedEditorEntity_=editorSelection_.Primary();physicsWorld_.ResetSimulation();
		if(confirmation->effect && confirmation->effect->HasRetainedPending()){prefabRecoveryBlocked_=true;prefabRecoveryWindowOpen_=true;}
		std::string diagnostic;const bool scanned=RefreshPrefabRecovery(diagnostic);
		if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))scene->ResetAnimations();
		genericWorldStatus_=prefabRecoveryBlocked_?"Applied source/Scene; pending archive recovery blocks further authoring":
			confirmation->admission.SourceChanged()?"Applied Prefab source and matching roots in this Scene; Undo available":
			confirmation->admission.WorldChanged()?"Refreshed Prefab baselines in this Scene; source unchanged; Undo available":"Prefab Apply was an exact no-op";
		if(!scanned)genericWorldStatus_+="; inventory: "+diagnostic;
		if(confirmation->field)prefabOverridesStatus_=genericWorldStatus_+"; refresh override snapshot.";
		error.clear();return true;
	}catch(const std::exception& exception){
		error=exception.what();std::string diagnostic;RefreshPrefabRecovery(diagnostic);
		genericWorldStatus_=(committed?"Prefab Apply committed; post-publication reporting failed: ":"Prefab Apply rejected: ")+error;
		return committed;
	}
}
bool Game::UnlinkEditorPrefab(EntityId root,std::string& error)
{
	if(!CanAuthorPrefab(error)){genericWorldStatus_=error;return false;}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::UnlinkPrefab(editorWorld_,root,editorSelection_,entityHistory_,error)){genericWorldStatus_="Prefab unlink rejected: "+error;return false;}
	documentState_.Observe(editorWorld_);physicsWorld_.ResetSimulation();genericWorldStatus_="Unlinked Prefab root; baked Entities retained";return true;
}

bool Game::CreateEditorEntity(const std::string& name,EntityId parent,EntityId& created,std::string& error)
{
	return CreateEditorEntityWithInitial(name,parent,created,error,{});
}

bool Game::CreateLuaScriptAsset(const std::filesystem::path& path,std::string& error)
{
	try {
		if (!CheckPrefabRecovery(error)) return false;
		if (const auto* blocked = SceneEditUnavailableReason(Engine::Editor::SceneEditAction::Duplicate))
			throw std::runtime_error(blocked);
		if (!path.is_absolute() || projectFile_.empty() || _wcsicmp(path.extension().c_str(),L".lua")!=0)
			throw std::runtime_error("Choose a .lua file inside this project's asset root");
		const auto root = std::filesystem::canonical(projectAssetRoot_);
		const auto project = std::filesystem::canonical(GetProjectRoot());
		const auto projectRelative = root.lexically_relative(project);
		if (projectRelative.empty() || *projectRelative.begin()==L"..")
			throw std::runtime_error("Lua creation requires a project-local asset root");
		const auto relative = Assets::MoveDetail::Relative(Base::PathToUtf8(
			path.lexically_normal().lexically_relative(projectAssetRoot_.lexically_normal())));
		Assets::MoveDetail::CheckParents(root,relative);
		const auto destination = root / relative;
		if (std::filesystem::exists(root / ".kubo/asset-move.pending.json") ||
			std::filesystem::exists(Assets::AssetRegistry::MetadataPath(destination)))
			throw std::runtime_error("Recover the pending asset move or choose a name without existing metadata");
		constexpr char source[] = R"(-- Attach to an Entity with an Is Trigger collider. Load Script Asset
-- to edit the properties in the Inspector. Scene names are relative to Assets.
properties = { visitor = "", destination_scene = "", sound = "", delay = 0.5 }

function on_trigger_enter(entity, other_id)
    local visitor = entity:property("visitor")
    if visitor ~= "" and scene.find(visitor) ~= other_id then return end
    local destination = entity:property("destination_scene")
    if destination == "" or not scene.change_scene(destination, entity:property("delay")) then return end
    local sound = entity:property("sound") -- Project-relative WAV path.
    if sound ~= "" then audio.play(sound, false, 1) end
end

function on_trigger_exit(entity, other_id)
    -- Called once when the two colliders stop overlapping.
end
)";
		if (!Base::AtomicFile::Write(destination,source,sizeof(source),error,false)) return false;
		if (!assetImportJob_.Start({GetProjectRoot(),projectAssetRoot_,{},false,projectFile_}))
			throw std::runtime_error("File created. Refresh Asset Browser to register it.");
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error=exception.what();
		return false;
	}
}

bool Game::PlaceSceneAsset(const std::string& guid,Vector3 position,std::string& error)
{
	if (!genericSceneSelected_ || !Engine::Editor::FinitePoint(position)) {
		error = "Placement requires a generic Scene and a finite position";
		return false;
	}
	const auto* asset = projectAssets_.FindByGuid(guid);
	if (!asset) {error = "Asset GUID is no longer registered";return false;}
	EntityId created{};
	if (asset->type == Assets::AssetType::Prefab)
		return InstantiateEditorPrefab(guid,0,created,error,position);
	const auto source = Assets::ResolveModelReference({{},guid},projectAssets_,GetProjectRoot(),error);
	if (source.empty()) return false;
	const auto relative = source.lexically_relative(GetProjectRoot());
	if (relative.empty() || *relative.begin() == L"..") {
		error = "Model must be inside the project";
		return false;
	}
	const Engine::Editor::EntityCreation initial{position,{Base::PathToUtf8(relative),guid}};
	if (!CreateEditorEntityWithInitial(Base::PathToUtf8(source.stem()),0,created,error,initial)) return false;
	genericWorldStatus_ = "Placed model in Scene";
	return true;
}

bool Game::CreateEditorEntityWithInitial(const std::string& name,EntityId parent,EntityId& created,
	std::string& error,const Engine::Editor::EntityCreation& initial)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Create requires an editing Scene without a pending document, drag, import or export";genericWorldStatus_=error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::CreateAuthoredEntity(editorWorld_,name,parent,editorSelection_,entityHistory_,created,error,initial)) {
		genericWorldStatus_="Create rejected: "+error;return false;
	}
	selectedEditorEntity_=editorSelection_.Primary();documentState_.Observe(editorWorld_);physicsWorld_.ResetSimulation();
	genericWorldStatus_="Created Entity";return true;
}

bool Game::DuplicateEditorSelection(std::string& error)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Duplicate requires an editing generic Scene without a pending document, drag, import or export";genericWorldStatus_=error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::DuplicateSelectedSubtrees(editorWorld_,editorSelection_,entityHistory_,error)) {
		genericWorldStatus_="Duplicate rejected: "+error;return false;
	}
	selectedEditorEntity_=editorSelection_.Primary();documentState_.Observe(editorWorld_);
	genericWorldStatus_="Duplicated selected subtrees";return true;
}

bool Game::DeleteEditorSelection(std::string& error)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Subtree delete requires an editing generic Scene without a pending document, drag, import or export";genericWorldStatus_=error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::DeleteSelectedSubtrees(editorWorld_,editorSelection_,entityHistory_,error)) {
		genericWorldStatus_="Subtree delete rejected: "+error;return false;
	}
	selectedEditorEntity_=0;documentState_.Observe(editorWorld_);
	genericWorldStatus_="Deleted selected subtrees; Undo restores Entities; asset files unchanged";return true;
}

bool Game::ReparentEditorSelection(EntityId parent,bool keepWorld,std::string& error)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Batch reparent requires an editing generic Scene without a pending document, drag, import or export";genericWorldStatus_=error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
	if(!Engine::Editor::ReparentSelectedSubtrees(editorWorld_,editorSelection_,parent,keepWorld,entityHistory_,error)) {
		genericWorldStatus_="Batch reparent rejected: "+error;return false;
	}
	documentState_.Observe(editorWorld_);physicsWorld_.ResetSimulation();
	genericWorldStatus_=keepWorld?"Reparented selected subtrees (World retained)":"Reparented selected subtrees (local TRS retained)";return true;
}

bool Game::TransformEditorSelection(const Matrix4x4& from,const Matrix4x4& to,int operation,std::string& error)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Group transform requires an idle editing generic Scene";genericWorldStatus_=error;return false;
	}
	editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);entityHistory_.Commit();
	if(!Engine::Editor::ApplyGroupTransform(editorWorld_,editorSelection_,from,to,operation,entityHistory_,error)) {genericWorldStatus_="Group transform rejected: "+error;return false;}
	entityHistory_.Commit();documentState_.Observe(editorWorld_);physicsWorld_.ResetSimulation();genericWorldStatus_="Transformed selected forest around shared pivot";return true;
}

bool Game::CanSaveSceneAs() const
{
	return CanOpenScene() && worldPlayback_.GetState()==WorldPlayback::State::Editing;
}

bool Game::SaveSceneAs(const std::filesystem::path& path,std::string& error,bool overwriteApproved)
{
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	try {
		if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || assetImportJob_.IsBusy() ||
			exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()!=WorldPlayback::State::Editing)
			throw std::runtime_error("Save As requires an idle generic Editor in Editing mode");
		Engine::Base::ProjectContext project;
		if(!project.Open(projectFile_,error)) return false;
		if(project.GetRootPath()!=GetProjectRoot()) throw std::runtime_error("Save As cannot switch the project root");
		std::filesystem::path target;
		if(!Engine::Editor::SaveProjectSceneAs(project,editorWorld_,path,overwriteApproved,target,error)) return false;
		const bool changedPath=genericWorldPath_!=target;
		entityHistory_.Commit();genericWorldPath_=target;documentState_.SavedAs(editorWorld_,target);
		hubPreviewRequested_ = true;
		if(changedPath) {mcpSession_.Stop();mcpStatus_="MCP disabled after Save As; enable again for the new document path";}
		genericWorldStatus_="Saved as: "+Engine::Base::PathToUtf8(target);error.clear();return true;
	} catch(const std::exception& exception) {error=exception.what();return false;}
}

bool Game::BrowseSaveSceneAs()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	wchar_t file[32768]=L"Untitled.scene";OPENFILENAMEW dialog{};
	dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=winApp->GetHwnd();dialog.lpstrFile=file;dialog.nMaxFile=32768;
	dialog.lpstrFilter=L"Scene (*.scene)\0*.scene\0JSON Scene (*.json)\0*.json\0\0";dialog.lpstrDefExt=L"scene";
	dialog.lpstrInitialDir=GetProjectRoot().c_str();dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST;
	if(!GetSaveFileNameW(&dialog)) {
		const auto code=CommDlgExtendedError();genericWorldStatus_=code ? "Scene save dialog failed: "+std::to_string(code) : "Save As cancelled";return false;
	}
	std::string error;
	std::error_code fileError;const bool replace=std::filesystem::exists(file,fileError);
	if(fileError) {genericWorldStatus_="Cannot inspect Scene save destination: "+fileError.message();return false;}
	if(replace && MessageBoxW(winApp->GetHwnd(),L"Overwrite this saved generic Scene? Non-Scene files will be refused.",L"Save Scene As",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) {
		genericWorldStatus_="Save As cancelled";return false;
	}
	if(!SaveSceneAs(file,error,replace)) {genericWorldStatus_="Save As failed: "+error;return false;}
	return true;
#else
	return false;
#endif
}

bool Game::CanOpenScene() const
{
	if(controllerEditor_.NeedsResolution() || controllerClosePending_)return false;
	std::string error;if(!CheckPrefabRecovery(error))return false;
	return editorMode_ && genericSceneSelected_ && genericWorldReady_ && !projectFile_.empty() &&
		!documentState_.HasPendingAction() && !gizmoDragActive_ && !assetImportJob_.IsBusy() &&
		exportJob_.GetState()!=Engine::Services::ExportJob::State::Running;
}

bool Game::RequestOpenScene(const std::filesystem::path& path,std::string& error)
{
	try {
		if(!CanOpenScene()) throw std::runtime_error("Open Scene requires a generic project and no pending action, gizmo, import or export");
		Engine::Base::ProjectContext project;
		if(!project.Open(projectFile_,error)) return false;
		if(project.GetRootPath()!=GetProjectRoot()) throw std::runtime_error("Scene Open cannot switch the active project root");
		std::string relative;
		if(!Engine::Editor::SelectProjectStartupScene(project,path,relative,error)) return false;
		if(worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
			if(!StopEditorPlayback(error)) return false;
			selectedEditorEntity_=editorWorld_.Find(authoredSelection_) ? authoredSelection_ : 0;
			if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) scene->ResetAnimations();
		}
		entityHistory_.Commit();documentState_.Observe(editorWorld_);
		if(!documentState_.RequestOpen(project.ResolvePath(relative))) throw std::runtime_error("Another document action is pending");
		error.clear();return true;
	} catch(const std::exception& exception) {error=exception.what();return false;}
}

bool Game::RequestNewScene(std::string& error)
{
	try {
		if(!CanOpenScene()) throw std::runtime_error("New Scene requires an idle generic project");
		RequestDocumentAction(Engine::Editor::EntityDocumentState::Action::New);
		if(!documentState_.HasPendingAction()) throw std::runtime_error("Could not request New Scene");
		error.clear();return true;
	} catch(const std::exception& exception) {error=exception.what();return false;}
}

void Game::BrowseOpenScene()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(!CanOpenScene()) return;
	wchar_t file[32768]{};OPENFILENAMEW dialog{};
	dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=winApp->GetHwnd();dialog.lpstrFile=file;dialog.nMaxFile=32768;
	dialog.lpstrFilter=L"Scene (*.scene;*.json)\0*.scene;*.json\0All files (*.*)\0*.*\0\0";
	dialog.lpstrInitialDir=GetProjectRoot().c_str();dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
	if(GetOpenFileNameW(&dialog)) {
		std::string error;if(!RequestOpenScene(file,error)) genericWorldStatus_="Open rejected: "+error;
	} else if(const auto code=CommDlgExtendedError()) genericWorldStatus_="Scene dialog failed: "+std::to_string(code);
#endif
}

bool Game::OpenGenericDocument(const std::filesystem::path& path,std::string& error)
{
	if(controllerEditor_.NeedsResolution() || controllerClosePending_){error="Resolve the Controller draft before changing the Scene";return false;}
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	try {
		if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || assetImportJob_.IsBusy() ||
			exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()!=WorldPlayback::State::Editing)
			throw std::runtime_error("Scene cannot be changed while an Editor operation is busy");
		Engine::Base::ProjectContext project;EntityWorld candidate;std::string relative;
		if(!project.Open(projectFile_,error)) return false;
		if(project.GetRootPath()!=GetProjectRoot()) throw std::runtime_error("Scene Open cannot switch the active project root");
		if(!Engine::Editor::SelectProjectStartupScene(project,path,relative,error,&candidate)) return false;
		const auto target=project.ResolvePath(relative);
		// Revalidated after Save/Discard. Failure above leaves the old document,
		// authored edits, history and live MCP session intact.
		CommitGenericDocument(std::move(candidate),target);
		genericWorldStatus_="Opened: "+Engine::Base::PathToUtf8(genericWorldPath_);error.clear();return true;
	} catch(const std::exception& exception) {error=exception.what();return false;}
}

void Game::CommitGenericDocument(EntityWorld candidate,const std::filesystem::path& path)
{
	const bool differentDocument = genericWorldPath_!=path;
	mcpSession_.Stop();mcpStatus_="MCP disabled after Scene change; enable again for the new document";
	scriptRuntime_.Shutdown(editorWorld_);scriptAudio_.Clear();entityAudio_.Clear();editorWorld_=std::move(candidate);genericWorldPath_=path;
	entityHistory_.Clear();documentState_.Opened(editorWorld_,path);physicsWorld_.ResetSimulation();
	if (differentDocument) {
		ResetEditorGridDefault();
		hierarchySearch_[0] = '\0';
		hierarchyType_ = HierarchyType::All;
	}
	selectedEditorEntity_=editorWorld_.GetEntities().empty() ? 0 : editorWorld_.GetEntities().front().id;
	editorSelection_.Replace(selectedEditorEntity_);
	authoredSelection_=selectedEditorEntity_;genericPlayerEntity_=0;
	for(const auto& entity:editorWorld_.GetEntities()) if(entity.name=="Player") {genericPlayerEntity_=entity.id;break;}
	gameScriptInputAllowed_=false;editorPickPending_=false;editorFocusPending_=false;
	if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) scene->ResetAnimations();
}

bool Game::NewGenericDocument(std::string& error)
{
	if(controllerEditor_.NeedsResolution() || controllerClosePending_){error="Resolve the Controller draft before changing the Scene";return false;}
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return false;}
	if(!editorMode_ || !genericSceneSelected_ || !genericWorldReady_ || gizmoDragActive_ || assetImportJob_.IsBusy() ||
		exportJob_.GetState()==Engine::Services::ExportJob::State::Running || worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="Cannot create a Scene while an Editor operation is busy";return false;
	}
	CommitGenericDocument(EntityWorld{},{});genericWorldStatus_="New untitled Scene; use Save or Save Scene As";error.clear();return true;
}

bool Game::ConfirmDocumentAction(DocumentDecision decision,std::string& error)
{
	if(decision!=DocumentDecision::Save && decision!=DocumentDecision::Discard && decision!=DocumentDecision::Cancel) {error="Invalid document decision";return false;}
	if(!documentState_.HasPendingAction()) {error="No document action is pending";return false;}
	if(decision==DocumentDecision::Cancel) {documentState_.Cancel();error.clear();return true;}
	if(assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || gizmoDragActive_) {
		error="Wait for the current Editor operation before continuing";return false;
	}
	if(decision==DocumentDecision::Save) {
		if(!SaveGenericDocument() || !documentState_.SavedAndProceed()) {error=genericWorldStatus_;return false;}
	} else documentState_.Discard();
	error.clear();return true;
}
bool Game::OpenControllerAssetEditor(const std::string& guid,std::string& error) {
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || controllerClosePending_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running){error="Controller editor requires an idle editing project";return false;}
    Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
    if(project.GetRootPath()!=GetProjectRoot()){error="Controller project root changed";return false;}
    return controllerEditor_.Open(project,projectAssets_,guid,error);
}
bool Game::ConfirmControllerClose(bool discard,std::string& error) {
    return ConfirmControllerClose(discard?DocumentDecision::Discard:DocumentDecision::Cancel,error);
}
bool Game::SaveControllerAsset(std::string& error) {
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || prefabRecoveryBlocked_ || gizmoDragActive_){error="Controller Save requires idle Editing mode";return false;}
    Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
    if(project.GetRootPath()!=GetProjectRoot()){error="Controller Save project root changed";return false;}
    auto& session=controllerEditor_.Session();
    if(session.HasPendingCreation() && !session.AdoptCreated(project,projectAssets_,error))return false;
    if(!session.Source())return SaveControllerAssetAs(controllerEditor_.SaveAsPath(),error);
    return controllerEditor_.Session().Save(project,projectAssets_,error)==Engine::Editor::AnimatorControllerSession::SaveOutcome::Saved;
}
bool Game::SaveControllerAssetAs(const std::string& assetRelative,std::string& error) {
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || prefabRecoveryBlocked_ || gizmoDragActive_){error="Controller Save As requires idle Editing mode";return false;}
    Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
    if(project.GetRootPath()!=GetProjectRoot()){error="Controller Save As project root changed";return false;}
    if(!controllerEditor_.ConfigureLayoutProject(project,error))return false;
    return controllerEditor_.Session().SaveAs(project,projectAssets_,assetRelative,error)==Engine::Editor::AnimatorControllerSession::SaveOutcome::Saved;
}
bool Game::NewControllerAssetEditor(std::string& error) {
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || controllerClosePending_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || prefabRecoveryBlocked_ || gizmoDragActive_){error="New Controller requires idle Editing mode";return false;}
    Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
    if(project.GetRootPath()!=GetProjectRoot()){error="New Controller project root changed";return false;}
    if(!controllerEditor_.ConfigureLayoutProject(project,error))return false;
    return controllerEditor_.New(error);
}
bool Game::ConfirmControllerClose(DocumentDecision decision,std::string& error) {
    if(!controllerClosePending_){error="No Controller exit decision pending";return false;}
    if(decision==DocumentDecision::Cancel){controllerClosePending_=false;error.clear();return true;}
    if(decision==DocumentDecision::Save){
        if(worldPlayback_.GetState()!=WorldPlayback::State::Editing){error="Stop playback before saving the Controller";return false;}
        if(!SaveControllerAsset(error))return false;
    }
    if(!controllerEditor_.Close(decision==DocumentDecision::Discard,error))return false;
    controllerClosePending_=false;RequestDocumentAction(Engine::Editor::EntityDocumentState::Action::Close);return true;
}

void Game::ReloadGenericDocument()
{
	std::string error;
	if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return;}
	if (!editorWorld_.Load(genericWorldPath_, error)) { genericWorldStatus_ = "Load failed: " + error; return; }
	entityHistory_.Clear();
	documentState_.Opened(editorWorld_, genericWorldPath_);
	physicsWorld_.ResetSimulation();
	selectedEditorEntity_ = editorWorld_.GetEntities().empty() ? 0 : editorWorld_.GetEntities().front().id;
	editorSelection_.Replace(selectedEditorEntity_);
	genericPlayerEntity_ = 0;
	for (const auto& entity : editorWorld_.GetEntities()) if (entity.name == "Player") { genericPlayerEntity_ = entity.id; break; }
	if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) scene->ResetAnimations();
	genericWorldStatus_ = "Loaded: " + Engine::Base::PathToUtf8(genericWorldPath_);
}

void Game::RequestDocumentAction(Engine::Editor::EntityDocumentState::Action action)
{
	if (sampleClosePending_) return;
	if(controllerEditor_.NeedsResolution()) {
		if(action==Engine::Editor::EntityDocumentState::Action::Close)controllerClosePending_=true;
		else genericWorldStatus_="Resolve the Controller draft before changing the Scene";
		return;
	}
	Engine::Scripting::ScriptRuntime::AnimatorReadScope animatorRead(scriptRuntime_,dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()));
	if(action!=Engine::Editor::EntityDocumentState::Action::Close){std::string error;if(!CheckPrefabRecovery(error)){genericWorldStatus_=error;return;}}
	if (documentState_.HasPendingAction()) return;
	if (action == Engine::Editor::EntityDocumentState::Action::Close && !genericSceneSelected_) {
		if (auto* sample = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
			if (sample->GetPlayback() != GamePlayScene::Playback::Editing && !sample->Stop()) {
				genericWorldStatus_ = sample->GetSceneFileStatus();
				return;
			}
			if (sample->HasUnsavedScene()) { sampleClosePending_ = true; return; }
		}
	}
	if (worldPlayback_.GetState() != WorldPlayback::State::Editing) {
		std::string error;
		if (!StopEditorPlayback(error)) { genericWorldStatus_ = error; return; }
		selectedEditorEntity_ = editorWorld_.Find(authoredSelection_) ? authoredSelection_ : 0;
		if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) scene->ResetAnimations();
	}
	entityHistory_.Commit();
	documentState_.Observe(editorWorld_);
	documentState_.Request(action);
}

void Game::DrawDocumentConfirmation()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(controllerClosePending_) {
		if(!ImGui::IsPopupOpen("Unsaved Controller before exit"))ImGui::OpenPopup("Unsaved Controller before exit");
		if(ImGui::BeginPopupModal("Unsaved Controller before exit",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("Resolve the Controller before the separate Scene close decision. Save requires stopped playback. Pending source acknowledgement must be retried, not discarded.");
			if(!controllerEditor_.Session().Source())controllerEditor_.DrawSaveAsPath();
			ImGui::BeginDisabled(worldPlayback_.GetState()!=WorldPlayback::State::Editing);
			if(ImGui::Button("Save Controller and continue")){std::string error;if(ConfirmControllerClose(DocumentDecision::Save,error))ImGui::CloseCurrentPopup();else genericWorldStatus_=error;}
			ImGui::EndDisabled();ImGui::SameLine();
			if(ImGui::Button("Discard Controller and continue")){std::string error;if(ConfirmControllerClose(true,error))ImGui::CloseCurrentPopup();else genericWorldStatus_=error;}
			ImGui::SameLine();if(ImGui::Button("Cancel exit")){std::string error;ConfirmControllerClose(false,error);ImGui::CloseCurrentPopup();}
			if(!genericWorldStatus_.empty())ImGui::TextWrapped("%s",genericWorldStatus_.c_str());
			ImGui::EndPopup();
		}
		return;
	}
	if (sampleClosePending_) {
		constexpr const char* title = "Unsaved sample scene";
		if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
		if (ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The sample scene has unsaved changes.");
			bool proceed = false;
			ImGui::BeginDisabled(assetImportJob_.IsBusy() || exportJob_.GetState() == Engine::Services::ExportJob::State::Running);
			if (ImGui::Button("Save and continue")) {
				if (auto* sample = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
					proceed = sample->SaveCurrentScene();
					genericWorldStatus_ = sample->GetSceneFileStatus();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard")) proceed = true;
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || proceed) {
				sampleClosePending_ = false;
				ImGui::CloseCurrentPopup();
			}
			if (proceed) {
				entityHistory_.Commit();
				documentState_.Observe(editorWorld_);
				documentState_.Request(Engine::Editor::EntityDocumentState::Action::Close);
			}
			if (!genericWorldStatus_.empty()) ImGui::TextWrapped("%s",genericWorldStatus_.c_str());
			ImGui::EndPopup();
		}
		return;
	}
	if (documentState_.NeedsConfirmation()) {
		if (!ImGui::IsPopupOpen("Unsaved scene")) ImGui::OpenPopup("Unsaved scene");
		if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The current scene has unsaved changes.");
			ImGui::TextWrapped("Save before continuing, discard the edits, or cancel.");
			if (ImGui::Button("Save and continue")) {
				std::string error;if(ConfirmDocumentAction(DocumentDecision::Save,error)) ImGui::CloseCurrentPopup();else genericWorldStatus_=error;
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard")) { std::string error;if(ConfirmDocumentAction(DocumentDecision::Discard,error)) ImGui::CloseCurrentPopup();else genericWorldStatus_=error; }
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) { std::string error;ConfirmDocumentAction(DocumentDecision::Cancel,error);ImGui::CloseCurrentPopup(); }
			if (!genericWorldStatus_.empty()) ImGui::TextWrapped("%s", genericWorldStatus_.c_str());
			ImGui::EndPopup();
		}
	}
	const auto target=documentState_.PendingTarget();
	const auto action = documentState_.TakeReadyAction();
	if (action == Engine::Editor::EntityDocumentState::Action::Close) {
		// A request delivered before the first Scene frame has nothing to refresh.
		hubPreviewCloseFrame_ = hubPreviewInitialAt_ != 0 && !documentState_.IsDirty() && !documentState_.Path().empty() &&
			worldPlayback_.GetState() == WorldPlayback::State::Editing && offscreenRenderManager->HasEditorView();
		endRequest_ = !hubPreviewCloseFrame_;
	}
	else if (action == Engine::Editor::EntityDocumentState::Action::Reload) ReloadGenericDocument();
	else if(action==Engine::Editor::EntityDocumentState::Action::Open) {std::string error;if(!OpenGenericDocument(target,error)) genericWorldStatus_="Open failed: "+error;}
	else if(action==Engine::Editor::EntityDocumentState::Action::New) {std::string error;if(!NewGenericDocument(error)) genericWorldStatus_="New failed: "+error;}
#endif
}

bool Game::InspectEditorPrefabOverrides(EntityId root,std::string& error)
{
	try {
		if(!editorMode_ || !genericWorldReady_)throw std::runtime_error("Prefab inspection requires a generic Editor World");
		Engine::Editor::PrefabOverrideReport candidate;
		if(!Engine::Editor::InspectPrefabOverrides(editorWorld_,root,candidate,error))return false;
		auto scene=documentState_.Path();
		prefabOverrides_=std::move(candidate);prefabOverridesScene_.swap(scene);
		prefabOverridesGeneration_=documentState_.Generation();
		prefabOverridesStatus_.clear();prefabOverridesOpen_=true;error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();return false;}
}

bool Game::InspectEditorPrefabThreeWay(EntityId root,std::string& error)
{
	try {
		if(!editorMode_ || !genericWorldReady_)throw std::runtime_error("Prefab comparison requires a generic Editor World");
		const auto* entity=editorWorld_.Find(root);if(!entity || entity->prefabLink.is_null())throw std::runtime_error("Comparison requires a linked Prefab root");
		Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))throw std::runtime_error(error);
		Engine::Editor::HeldProjectPrefabRevision read;
		if(!read.Open(project,projectAssets_,entity->prefabLink["sourceGuid"].get<crude_json::string>(),error))throw std::runtime_error(error);
		auto candidate=std::make_unique<PrefabThreeWaySnapshot>();candidate->scene=documentState_.Path();candidate->revision=read.Snapshot();
		candidate->generation=documentState_.Generation();
		if(!Engine::Editor::InspectPrefabThreeWay(editorWorld_,root,candidate->revision.definition,candidate->revision.guid,candidate->report,error))throw std::runtime_error(error);
		// Complete all copying while source/metadata read ownership is retained.
		// A failed refresh keeps the entire previous report/revision/Scene binding.
		prefabThreeWay_=std::move(candidate);prefabThreeWayPage_=0;prefabThreeWayStatus_.clear();prefabThreeWayOpen_=true;error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();return false;}
}
void Game::DrawPrefabThreeWayWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(!prefabThreeWayOpen_)return;
	ImGui::SetNextWindowSize(ImVec2(760,520),ImGuiCond_FirstUseEver);
	if(ImGui::Begin("Prefab Three-Way",&prefabThreeWayOpen_)) {
		ImGui::TextWrapped("Read-only snapshot of stored baseline / current source / instance. No source, Scene, selection or history is changed. Refresh after edits or external source changes; this is not a live source monitor.");
		if(!prefabThreeWay_)ImGui::TextWrapped("Select a linked root in Hierarchy and choose Compare primary Prefab with current source.");
		else {
			const auto root=prefabThreeWay_->report.root;const bool sameScene=prefabThreeWay_->scene==documentState_.Path() && prefabThreeWay_->generation==documentState_.Generation();
			ImGui::Text("Root Scene Entity: %llu",static_cast<unsigned long long>(root));
			ImGui::TextWrapped("Source: %s",Engine::Base::PathToUtf8(prefabThreeWay_->revision.file).c_str());
			ImGui::TextWrapped("GUID: %s",prefabThreeWay_->report.sourceGuid.c_str());
			ImGui::TextWrapped("Scene: %s",Engine::Base::PathToUtf8(prefabThreeWay_->scene).c_str());
			if(!sameScene)ImGui::TextWrapped("Scene changed: this retained snapshot belongs to the previous Scene. Compare a root in the new Scene to replace it.");
			ImGui::BeginDisabled(!sameScene);
			if(ImGui::Button("Refresh current source comparison")){std::string error;if(!InspectEditorPrefabThreeWay(root,error))prefabThreeWayStatus_="Refresh rejected; previous snapshot retained: "+error;}
			ImGui::EndDisabled();
			if(ImGui::Combo("Changes",&prefabThreeWayFilter_,"All changes\0Conflicts\0Source only\0Instance only\0Converged\0"))prefabThreeWayPage_=0;
			const auto matches=[&](const auto& entry){using C=Engine::Editor::PrefabThreeWayChange;return prefabThreeWayFilter_==0 || entry.change==(prefabThreeWayFilter_==1?C::Conflict:prefabThreeWayFilter_==2?C::SourceOnly:prefabThreeWayFilter_==3?C::InstanceOnly:C::Converged);};
			const auto& report=prefabThreeWay_->report;size_t count=0;for(const auto& entry:report.entries)if(matches(entry))++count;
			constexpr size_t PageSize=64;const auto pages=(std::max)(size_t(1),(count+PageSize-1)/PageSize);prefabThreeWayPage_=(std::min)(prefabThreeWayPage_,int(pages-1));
			ImGui::Text("Matching differences: %zu / %zu; page %d / %zu",count,report.entries.size(),prefabThreeWayPage_+1,pages);
			ImGui::BeginDisabled(prefabThreeWayPage_==0);if(ImGui::Button("Previous differences"))--prefabThreeWayPage_;ImGui::EndDisabled();ImGui::SameLine();
			ImGui::BeginDisabled(size_t(prefabThreeWayPage_+1)>=pages);if(ImGui::Button("Next differences"))++prefabThreeWayPage_;ImGui::EndDisabled();
			ImGui::TextWrapped("Names remain instance-owned during Apply. Arrays are atomic. Scene ID 0 means a source Entity not mapped into this instance. New source-only Entities can be adopted with missing new-source parents. Existing/deleted parent conflicts and Entity removal need separate resolution.");
			std::string authoringError;const bool authoringAllowed=sameScene && !prefabApply_ && !prefabFieldRevert_ && CanAuthorPrefab(authoringError);
			size_t matched=0;for(size_t index=0;index<report.entries.size();++index) {
				const auto& entry=report.entries[index];if(!matches(entry))continue;const auto ordinal=matched++;
				if(ordinal<size_t(prefabThreeWayPage_)*PageSize || ordinal>=size_t(prefabThreeWayPage_+1)*PageSize)continue;
				using C=Engine::Editor::PrefabThreeWayChange;const auto change=entry.change==C::Conflict?"Conflict":entry.change==C::Converged?"Converged":entry.change==C::SourceOnly?"Source only":"Instance only";
				const auto kind=entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence?"Entity presence":entry.kind==Engine::Editor::PrefabThreeWayKind::InstanceName?"Instance-owned name":"Field";
				const auto nameBytes=Engine::Editor::PrefabOverrideDisplayPrefix(entry.name,256),pathBytes=Engine::Editor::PrefabOverrideDisplayPrefix(entry.path,1024);
				ImGui::PushID(static_cast<int>(index));
				if(ImGui::TreeNode("comparison","%s | %s | local %llu / Scene %llu | %.*s | %.*s",change,kind,static_cast<unsigned long long>(entry.local),static_cast<unsigned long long>(entry.scene),static_cast<int>(nameBytes),entry.name.c_str(),static_cast<int>(pathBytes),entry.path.c_str())) {
					if(nameBytes<entry.name.size() || pathBytes<entry.path.size())ImGui::TextUnformatted("(row label truncated; retained snapshot is complete)");
					const auto show=[](const char* label,const std::optional<std::string>& value){ImGui::TextUnformatted(label);if(!value)ImGui::TextUnformatted("(absent)");else {const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(*value,2048);ImGui::TextWrapped("%.*s",static_cast<int>(bytes),value->c_str());if(bytes<value->size())ImGui::TextUnformatted("(display truncated; complete value retained)");}};
					show("Stored instance baseline",entry.baseline);show("Source at snapshot",entry.source);show("Instance at snapshot",entry.instance);
					const bool field=entry.kind==Engine::Editor::PrefabThreeWayKind::Field || entry.kind==Engine::Editor::PrefabThreeWayKind::InstanceName;
					ImGui::BeginDisabled(!field || !entry.scene || !authoringAllowed || prefabApply_ || prefabFieldRevert_);
					if(ImGui::Button("Adopt source value for this field...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabFieldAdoption(root,report.sourceGuid,entry,error))prefabThreeWayStatus_="Adoption preview rejected: "+error;}
					ImGui::EndDisabled();
					const bool addition=entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence && entry.change==Engine::Editor::PrefabThreeWayChange::SourceOnly && !entry.baseline && !entry.instance && entry.source;
					if(addition){ImGui::BeginDisabled(!authoringAllowed || prefabApply_ || prefabFieldRevert_);if(ImGui::Button("Adopt this new source Entity and missing new parents...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabEntityAddition(root,report.sourceGuid,entry,error))prefabThreeWayStatus_="Entity addition preview rejected: "+error;}ImGui::EndDisabled();}
					const bool removal=entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence && entry.baseline && !entry.source;
					if(removal){ImGui::BeginDisabled(!authoringAllowed || prefabApply_ || prefabFieldRevert_);if(ImGui::Button("Review source Entity removal and descendant dependencies...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabEntityRemoval(root,report.sourceGuid,entry,error))prefabThreeWayStatus_="Entity removal preview rejected: "+error;}ImGui::EndDisabled();}
					const bool restoration=entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence && entry.baseline && entry.source && !entry.instance;
					if(restoration){ImGui::BeginDisabled(!authoringAllowed || prefabApply_ || prefabFieldRevert_);if(ImGui::Button("Restore this deleted mapped Entity and missing parents...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabEntityRestoration(root,report.sourceGuid,entry,error))prefabThreeWayStatus_="Entity restoration preview rejected: "+error;}ImGui::EndDisabled();}
					ImGui::TreePop();
				}ImGui::PopID();
			}
		}
		if(!prefabThreeWayStatus_.empty())ImGui::TextWrapped("%s",prefabThreeWayStatus_.c_str());
	}ImGui::End();
#endif
}

bool Game::RequestEditorPrefabFieldRevert(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,std::string& error)
{
	return RequestEditorPrefabFieldRevertInternal(root,entry,nullptr,nullptr,error);
}
bool Game::RequestEditorPrefabFieldAdoption(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error)
{
	Engine::Editor::PrefabOverrideEntry field;
	try {
		field.kind=entry.kind==Engine::Editor::PrefabThreeWayKind::InstanceName?Engine::Editor::PrefabOverrideKind::InstanceName:Engine::Editor::PrefabOverrideKind::Field;
		field.local=entry.local;field.scene=entry.scene;field.name=entry.name;field.path=entry.path;field.baseline=entry.baseline;field.instance=entry.instance;
	}catch(const std::exception& exception){error=exception.what();return false;}
	return RequestEditorPrefabFieldRevertInternal(root,field,&entry,&guid,error);
}
bool Game::RequestEditorPrefabEntityAddition(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error)
{
	try {Engine::Editor::PrefabOverrideEntry display;display.kind=Engine::Editor::PrefabOverrideKind::EntityAdded;display.local=entry.local;display.scene=entry.scene;display.name=entry.name;display.path=entry.path;display.baseline=entry.baseline;display.instance=entry.instance;
		return RequestEditorPrefabFieldRevertInternal(root,display,&entry,&guid,error,true);
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::ConfirmEditorPrefabEntityAddition(std::string& error)
{
	if(!HasPendingPrefabEntityAddition()){error="No source Entity addition is pending";return false;}return ConfirmEditorPrefabFieldRevertInternal(error,false,true);
}
bool Game::RequestEditorPrefabEntityRemoval(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error)
{
	try {Engine::Editor::PrefabOverrideEntry display;display.kind=Engine::Editor::PrefabOverrideKind::EntityRemoved;display.local=entry.local;display.scene=entry.scene;display.name=entry.name;display.path=entry.path;display.baseline=entry.baseline;display.instance=entry.instance;
		return RequestEditorPrefabFieldRevertInternal(root,display,&entry,&guid,error,false,true);
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::ConfirmEditorPrefabEntityRemoval(std::string& error,bool approveConflicts)
{
	if(!HasPendingPrefabEntityRemoval()){error="No source Entity removal is pending";return false;}return ConfirmEditorPrefabFieldRevertInternal(error,approveConflicts,false,true);
}
bool Game::RequestEditorPrefabEntityRestoration(EntityId root,const std::string& guid,const Engine::Editor::PrefabThreeWayEntry& entry,std::string& error)
{
	try {Engine::Editor::PrefabOverrideEntry display;display.kind=Engine::Editor::PrefabOverrideKind::EntityRemoved;display.local=entry.local;display.scene=entry.scene;display.name=entry.name;display.path=entry.path;display.baseline=entry.baseline;display.instance=entry.instance;
		return RequestEditorPrefabFieldRevertInternal(root,display,&entry,&guid,error,false,false,true);
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::ConfirmEditorPrefabEntityRestoration(std::string& error,bool approveConflicts)
{
	if(!HasPendingPrefabEntityRestoration()){error="No source Entity restoration is pending";return false;}return ConfirmEditorPrefabFieldRevertInternal(error,approveConflicts,false,false,true);
}
bool Game::RequestEditorPrefabFieldRevertInternal(EntityId root,const Engine::Editor::PrefabOverrideEntry& entry,const Engine::Editor::PrefabThreeWayEntry* adoption,const std::string* expectedGuid,std::string& error,bool entityAddition,bool entityRemoval,bool entityRestoration)
{
	try {
		if(HasPendingPrefabRevert() || prefabApply_ || prefabFieldRevert_)throw std::runtime_error("A Prefab confirmation is already pending");
		if(!CanAuthorPrefab(error))throw std::runtime_error(error);
		const auto* entity=editorWorld_.Find(root);
		if(!entity || entity->prefabLink.is_null())throw std::runtime_error("Field Revert requires a linked root");
		if(expectedGuid && entity->prefabLink["sourceGuid"].get<crude_json::string>()!=*expectedGuid)throw std::runtime_error("Field adoption source GUID binding changed");
		Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))throw std::runtime_error(error);
		Engine::Editor::HeldProjectPrefabRevision read;
		if(!read.Open(project,projectAssets_,entity->prefabLink["sourceGuid"].get<crude_json::string>(),error))throw std::runtime_error(error);
		auto confirmation=std::make_unique<PrefabFieldRevertConfirmation>();
		confirmation->root=root;confirmation->primary=selectedEditorEntity_;confirmation->highWater=editorWorld_.AllocationHighWatermark();
		confirmation->revision=read.Snapshot();confirmation->entry=entry;
		if(adoption)confirmation->adoption=*adoption;
		confirmation->entityAddition=entityAddition;
		confirmation->entityRemoval=entityRemoval;
		confirmation->entityRestoration=entityRestoration;
		const auto before=editorWorld_.Capture();confirmation->beforeWorld=before.dump();confirmation->beforeSelection=editorSelection_.Capture();
		confirmation->beforeHistory=entityHistory_;confirmation->beforeDocument=documentState_;
		confirmation->nextHistory=entityHistory_;confirmation->nextSelection=editorSelection_;auto nextDocument=documentState_;
		const bool structural=entityAddition || entityRemoval || entityRestoration;
		if(entityRemoval){Engine::Editor::PrefabEntityRemovalPlan plan;
			if(!Engine::Editor::BuildPrefabEntityRemovalPlan(editorWorld_,root,confirmation->revision.definition,confirmation->revision.guid,*adoption,plan,error))throw std::runtime_error(error);
			confirmation->removals=std::move(plan.removals);
			confirmation->nextWorld=std::move(plan.world);
		}
		if(entityRestoration){Engine::Editor::PrefabEntityRestorationPlan plan;
			if(!Engine::Editor::BuildPrefabEntityRestorationPlan(editorWorld_,root,confirmation->revision.definition,confirmation->revision.guid,*adoption,plan,error))throw std::runtime_error(error);
			confirmation->restorations=std::move(plan.restorations);
			confirmation->nextWorld=std::move(plan.world);
		}
		if(entityAddition && !Engine::Editor::BuildPrefabEntityAdditionCandidate(editorWorld_,root,confirmation->revision.definition,confirmation->revision.guid,*adoption,confirmation->nextWorld,error))throw std::runtime_error(error);
		if(structural){
			// This plan was built immediately above under the held source reader
			// and on this authoring thread. Reuse its World rather than rebuilding
			// it through Adopt*. No public mutable-plan publication API is added.
			Engine::Editor::EntitySelection checked;
			if(!checked.Restore(editorWorld_,confirmation->beforeSelection,error))throw std::runtime_error(error);
			if(entityRemoval){auto primary=confirmation->nextSelection.Primary();confirmation->nextSelection.Synchronize(confirmation->nextWorld,primary);}
			const auto afterSelection=confirmation->nextSelection.Capture();
			if(!checked.Restore(confirmation->nextWorld,afterSelection,error))throw std::runtime_error(error);
			confirmation->nextHistory.Commit();
			if(!confirmation->nextHistory.Record(before,confirmation->nextWorld,confirmation->beforeSelection,afterSelection,error))throw std::runtime_error(error);
			confirmation->nextHistory.Commit();nextDocument.ObservePrepared(nextDocument.WouldBeDirty(confirmation->nextWorld));
		}else {
			confirmation->nextWorld=editorWorld_;
			const bool prepared=adoption?
				Engine::Editor::AdoptPrefabField(confirmation->nextWorld,root,confirmation->revision.definition,confirmation->revision.guid,*adoption,editorSelection_,confirmation->nextHistory,error,&nextDocument,true):
				Engine::Editor::RevertPrefabField(confirmation->nextWorld,root,confirmation->revision.definition,confirmation->revision.guid,entry,editorSelection_,confirmation->nextHistory,error,&nextDocument);
			if(!prepared)throw std::runtime_error(error);
		}
		if(entityAddition)for(const auto& added:confirmation->nextWorld.GetEntities())if(!editorWorld_.Find(added.id))confirmation->addedEntities.push_back(added.id);
		confirmation->nextDirty=nextDocument.IsDirty();
		if(entityAddition || entityRemoval || entityRestoration)confirmation->sourceValue=adoption->source;
		else {std::vector<std::string> tokens;if(!Engine::Editor::PrefabOverrideTokens(entry.path,tokens,error))throw std::runtime_error(error);
		for(const auto& node:confirmation->revision.definition["world"]["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(entry.local)) {
			if(const auto* value=Engine::Editor::PrefabOverrideValue(node,tokens))confirmation->sourceValue=value->dump();
		}}
		confirmation->comparison=Engine::Editor::ClassifyPrefabThreeWayValues(entry.baseline,confirmation->sourceValue,entry.instance);
		prefabFieldRevert_=std::move(confirmation);error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::ConfirmEditorPrefabFieldRevert(std::string& error)
{
	return ConfirmEditorPrefabFieldRevert(error,false);
}
bool Game::ConfirmEditorPrefabFieldAdoption(std::string& error,bool approveConflict)
{
	if(!HasPendingPrefabFieldAdoption()){error="No source field adoption is pending";return false;}
	return ConfirmEditorPrefabFieldRevert(error,approveConflict);
}
bool Game::ConfirmEditorPrefabFieldRevert(std::string& error,bool approveConflict)
{
	return ConfirmEditorPrefabFieldRevertInternal(error,approveConflict,false);
}
bool Game::ConfirmEditorPrefabFieldRevertInternal(std::string& error,bool approveConflict,bool entityAddition,bool entityRemoval,bool entityRestoration)
{
	static_assert(std::is_nothrow_move_assignable_v<EntityWorld>);
	static_assert(std::is_nothrow_move_assignable_v<Engine::Editor::EntitySelection>);
	if(prefabFieldRevert_ && (prefabFieldRevert_->entityAddition!=entityAddition || prefabFieldRevert_->entityRemoval!=entityRemoval || prefabFieldRevert_->entityRestoration!=entityRestoration)){error="Prefab confirmation kind changed; use the matching operation";return false;}
	auto confirmation=std::move(prefabFieldRevert_);bool committed=false;
	try {
		if(!confirmation)throw std::runtime_error("No field Revert confirmation is pending");
		const bool removalConflict=confirmation->entityRemoval && std::any_of(confirmation->removals.begin(),confirmation->removals.end(),[](const auto& row){return row.change==Engine::Editor::PrefabThreeWayChange::Conflict;});
		const bool restorationConflict=confirmation->entityRestoration && std::any_of(confirmation->restorations.begin(),confirmation->restorations.end(),[](const auto& row){return row.change==Engine::Editor::PrefabThreeWayChange::Conflict;});
		if((restorationConflict || removalConflict || confirmation->comparison==Engine::Editor::PrefabThreeWayChange::Conflict) && !approveConflict)
			throw std::runtime_error("Field Revert conflict requires explicit approval to replace the instance value");
		if(!CanAuthorPrefab(error))throw std::runtime_error(error);
		if(!documentState_.HasSameState(confirmation->beforeDocument) || editorWorld_.Capture().dump()!=confirmation->beforeWorld ||
			editorWorld_.AllocationHighWatermark()!=confirmation->highWater || selectedEditorEntity_!=confirmation->primary ||
			editorSelection_.Capture()!=confirmation->beforeSelection || !entityHistory_.HasSameState(confirmation->beforeHistory))throw std::runtime_error("Field Revert admission changed; request confirmation again");
		Engine::Base::ProjectContext project;if(!OpenPrefabProject(project,error))throw std::runtime_error(error);
		Engine::Editor::HeldProjectPrefabRevision read;
		if(!read.Open(project,projectAssets_,confirmation->revision.guid,error,&confirmation->revision))throw std::runtime_error(error);
		// The read handles exclude source/metadata writers through no-throw
		// Scene/history/dirty publication. No source file is changed by Revert.
		editorWorld_=std::move(confirmation->nextWorld);editorSelection_=std::move(confirmation->nextSelection);selectedEditorEntity_=editorSelection_.Primary();entityHistory_.Swap(confirmation->nextHistory);
		documentState_.ObservePrepared(confirmation->nextDirty);committed=true;
		physicsWorld_.ResetSimulation();if(auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))scene->ResetAnimations();
		prefabOverridesStatus_="Selected field reverted to current source; refresh snapshot. Undo available.";
		if(confirmation->adoption)prefabThreeWayStatus_="Selected source field adopted; refresh comparison. Undo available. Source unchanged; save the Scene separately.";
		if(confirmation->entityAddition)prefabThreeWayStatus_="New source Entity and required missing new parents adopted; refresh comparison. Undo available. Source unchanged; save the Scene separately.";
		if(confirmation->entityRemoval){prefabOverridesStatus_="Source-deleted Entity closure removed; refresh snapshot. Undo available.";prefabThreeWayStatus_="Source Entity removal and reviewed descendant dependencies adopted; refresh comparison. Undo available. Source unchanged; save the Scene separately.";}
		if(confirmation->entityRestoration){prefabOverridesStatus_="Deleted mapped Entity and missing parents restored; refresh snapshot. Undo available.";prefabThreeWayStatus_="Mapped Entity restored from source with reviewed missing parents; refresh comparison. Undo available. Source unchanged; save the Scene separately.";}
		error.clear();return true;
	}catch(const std::exception& exception){if(committed){error.clear();return true;}error=exception.what();prefabOverridesStatus_="Field Revert rejected: "+error;return false;}
}

void Game::DrawPrefabOverridesWindow()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(!prefabOverridesOpen_ && !prefabFieldRevert_ && !(prefabApply_ && prefabApply_->field))return;
	if(prefabOverridesOpen_) {
	ImGui::SetNextWindowSize(ImVec2(720,480),ImGuiCond_FirstUseEver);
	if(ImGui::Begin("Prefab Overrides",&prefabOverridesOpen_)) {
		ImGui::TextWrapped("Snapshot against the saved instance baseline, not the current source. Inspection/refresh does not change source, Scene or history. Field Revert/Apply open separate confirmations against freshly held source. Refresh after edits.");
		if(!prefabOverrides_.root)ImGui::TextWrapped("Select a linked Prefab root in Hierarchy and choose Inspect primary Prefab overrides to capture a snapshot.");
		ImGui::Text("Root Scene Entity: %llu",static_cast<unsigned long long>(prefabOverrides_.root));
		ImGui::TextWrapped("Source GUID: %s",prefabOverrides_.sourceGuid.c_str());
		ImGui::TextWrapped("Scene: %s",Engine::Base::PathToUtf8(prefabOverridesScene_).c_str());
		const bool sameScene=prefabOverridesScene_==documentState_.Path() && prefabOverridesGeneration_==documentState_.Generation();
		if(!sameScene)ImGui::TextWrapped("The open Scene changed. Inspect a linked root in the new Scene to replace this snapshot.");
		ImGui::BeginDisabled(!sameScene || !prefabOverrides_.root);
		if(ImGui::Button("Refresh snapshot")) {
			std::string error;if(!InspectEditorPrefabOverrides(prefabOverrides_.root,error))prefabOverridesStatus_="Refresh rejected; previous snapshot retained: "+error;
		}
		if(ImGui::Button("Compare current source")){std::string error;if(!InspectEditorPrefabThreeWay(prefabOverrides_.root,error))prefabOverridesStatus_="Comparison rejected: "+error;}
		ImGui::EndDisabled();
		if(!prefabOverridesStatus_.empty())ImGui::TextWrapped("%s",prefabOverridesStatus_.c_str());
		ImGui::TextWrapped("Names are instance-owned during Apply propagation. Revert resets them. Arrays (including TRS vectors) are compared as whole values.");
		ImGui::Text("Differences: %llu",static_cast<unsigned long long>(prefabOverrides_.entries.size()));
		if(prefabOverrides_.root && prefabOverrides_.entries.empty())ImGui::TextUnformatted("No changes against the stored baseline.");
		std::string authoringError;const bool authoringAllowed=sameScene && !prefabApply_ && !prefabFieldRevert_ && CanAuthorPrefab(authoringError);
		for(size_t index=0;index<prefabOverrides_.entries.size();++index) {
			const auto& entry=prefabOverrides_.entries[index];
			const char* kind=entry.kind==Engine::Editor::PrefabOverrideKind::EntityAdded?"Entity added":entry.kind==Engine::Editor::PrefabOverrideKind::EntityRemoved?"Entity removed":entry.kind==Engine::Editor::PrefabOverrideKind::InstanceName?"Instance-owned name":"Field override";
			ImGui::PushID(static_cast<int>(index));
			if(ImGui::TreeNode("difference","%s | local %llu / Scene %llu | %s | %s",kind,static_cast<unsigned long long>(entry.local),static_cast<unsigned long long>(entry.scene),entry.name.c_str(),entry.path.c_str())) {
				const auto show=[](const char* label,const std::optional<std::string>& value){
					ImGui::TextUnformatted(label);
					if(!value)ImGui::TextUnformatted("(absent)");
					else {const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(*value,2048);ImGui::TextWrapped("%.*s",static_cast<int>(bytes),value->c_str());if(value->size()>bytes)ImGui::TextUnformatted("(display truncated; report retains the complete value)");}
				};
				show("Stored baseline",entry.baseline);show("Instance snapshot",entry.instance);
				const bool field=entry.kind==Engine::Editor::PrefabOverrideKind::Field || entry.kind==Engine::Editor::PrefabOverrideKind::InstanceName;
				ImGui::BeginDisabled(!field || !authoringAllowed || prefabApply_ || prefabFieldRevert_);
				// Settle preceding Inspector edits before freezing the confirmation;
				// the ordinary end-of-frame Commit must not invalidate this preview.
				if(ImGui::Button("Revert this field to current source...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabFieldRevert(prefabOverrides_.root,entry,error))prefabOverridesStatus_="Preview rejected: "+error;}
				if(ImGui::Button("Apply this field to source...")){entityHistory_.Commit();documentState_.Observe(editorWorld_);std::string error;if(!RequestEditorPrefabFieldApply(prefabOverrides_.root,entry,error))prefabOverridesStatus_="Preview rejected: "+error;}
				ImGui::EndDisabled();ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
	ImGui::End();
	}
	if(prefabApply_ && prefabApply_->field && !ImGui::IsPopupOpen("Confirm Prefab Field Apply"))ImGui::OpenPopup("Confirm Prefab Field Apply");
	if(ImGui::BeginPopupModal("Confirm Prefab Field Apply",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
		if(prefabRecoveryBlocked_ || !prefabApply_ || !prefabApply_->field){if(!prefabApply_ || prefabApply_->field)CancelEditorPrefabApply();ImGui::CloseCurrentPopup();}
		else {
			auto& pending=*prefabApply_;const auto& field=*pending.field;
			ImGui::TextWrapped("Source: %s",Engine::Base::PathToUtf8(pending.revision.file).c_str());
			ImGui::Text("Local %llu / Scene %llu; matching roots: %zu",static_cast<unsigned long long>(field.local),static_cast<unsigned long long>(field.scene),pending.instances.size());
			ImGui::TextWrapped("Field: %s",field.path.c_str());
			const auto show=[](const char* label,const std::optional<std::string>& value){
				ImGui::TextUnformatted(label);if(!value)ImGui::TextUnformatted("(absent)");
				else {const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(*value,2048);ImGui::TextWrapped("%.*s",static_cast<int>(bytes),value->c_str());if(bytes<value->size())ImGui::TextUnformatted("(display truncated; complete value is retained)");}
			};
			show("Stored instance baseline",field.baseline);show("Current source value",pending.sourceValue);show("Selected instance value to apply",field.instance);
			ImGui::TextUnformatted(pending.comparison==Engine::Editor::PrefabThreeWayChange::Conflict?"Conflict: source and instance changed differently":pending.comparison==Engine::Editor::PrefabThreeWayChange::Converged?"Converged: source and instance have the same changed value":pending.comparison==Engine::Editor::PrefabThreeWayChange::InstanceOnly?"Only the instance changed":pending.comparison==Engine::Editor::PrefabThreeWayChange::SourceOnly?"Only the source changed":"Unchanged");
			const bool conflict=pending.comparison==Engine::Editor::PrefabThreeWayChange::Conflict;
			if(conflict)ImGui::Checkbox("Replace the conflicting source field with the instance value",&pending.conflictApproved);
			ImGui::TextWrapped("Only this field is applied. Matching roots in this open Scene inherit it unless independently overridden; names remain instance-owned. Unrelated fields and structure stay unchanged. Source/Scene/selection/history/document are revalidated. Undo restores source and Scene; save the Scene separately. Closed Scenes are not rewritten.");
			ImGui::TextUnformatted(pending.admission.SourceChanged()?"Source file and Scene baselines will change":"Source unchanged; Scene-only baseline refresh");
			ImGui::BeginDisabled(conflict && !pending.conflictApproved);
			if(ImGui::Button("Confirm field Apply")){const bool approved=pending.conflictApproved;std::string error;if(!ConfirmEditorPrefabApply(error,approved))prefabOverridesStatus_="Field Apply rejected: "+error;ImGui::CloseCurrentPopup();}
			ImGui::EndDisabled();
			ImGui::SameLine();if(ImGui::Button("Cancel field Apply")){CancelEditorPrefabApply();ImGui::CloseCurrentPopup();}
		}
		ImGui::EndPopup();
	}
	if(prefabFieldRevert_ && !ImGui::IsPopupOpen("Confirm Prefab Field Revert"))ImGui::OpenPopup("Confirm Prefab Field Revert");
	if(ImGui::BeginPopupModal("Confirm Prefab Field Revert",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
		if(prefabRecoveryBlocked_ || !prefabFieldRevert_){CancelEditorPrefabFieldRevert();ImGui::CloseCurrentPopup();}
		else {
			auto& pending=*prefabFieldRevert_;
			const bool adoption=pending.adoption.has_value();
			const bool addition=pending.entityAddition;
			const bool removal=pending.entityRemoval;
			const bool restoration=pending.entityRestoration;
			ImGui::TextUnformatted(restoration?"Restore a deleted mapped Entity and only required missing parents":removal?"Review source-deleted Entity closure before removal":addition?"Adopt one new source Entity and its required missing new parents":adoption?"Adopt one source field into this instance and its baseline":"Revert one instance override to the current source");
			if(addition)ImGui::TextWrapped("Adds only this Entity and missing new-source ancestors. Children are not imported automatically. Existing/deleted parents are not reset/restored. Other fields and instances stay unchanged.");
			if(addition){ImGui::Text("New Scene Entities including dependencies: %zu",pending.addedEntities.size());size_t shown=0;for(const auto id:pending.addedEntities){if(shown++>=16)break;const auto* entity=pending.nextWorld.Find(id);const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(entity->name,256);ImGui::Text("New %llu / parent %llu | %.*s",static_cast<unsigned long long>(id),static_cast<unsigned long long>(entity->parent),static_cast<int>(bytes),entity->name.c_str());}if(pending.addedEntities.size()>16)ImGui::TextUnformatted("(first 16 dependencies shown; cancel to inspect the complete source hierarchy)");}
			ImGui::TextWrapped("Source: %s",Engine::Base::PathToUtf8(pending.revision.file).c_str());
			ImGui::Text("Local %llu / Scene %llu",static_cast<unsigned long long>(pending.entry.local),static_cast<unsigned long long>(pending.entry.scene));
			if(restoration)ImGui::TextWrapped("Restores only this Entity and missing current-source parents. Known tombstone Scene IDs are retained; new-source parents get fresh IDs. Children/siblings are not restored automatically. Existing anchors, unrelated fields/deletions/instances and selection remain unchanged; save separately.");
			else if(removal)ImGui::TextWrapped("Removes the reviewed baseline/live descendant closure, including moved children and absent baseline tombstones. Source-surviving and instance-owned child dependencies are protected. Selection is pruned; Undo restores it. Source files and other instances stay unchanged; save the Scene separately.");
			else {ImGui::TextWrapped("Field: %s",pending.entry.path.c_str());ImGui::TextWrapped("Only this field and its stored baseline will change. Source files and unrelated overrides remain unchanged. Save the Scene separately.");}
			const auto show=[](const char* label,const std::optional<std::string>& value){ImGui::TextUnformatted(label);if(!value)ImGui::TextUnformatted("(absent)");else {const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(*value,2048);ImGui::TextWrapped("%.*s",static_cast<int>(bytes),value->c_str());if(bytes<value->size())ImGui::TextUnformatted("(display truncated; complete value is retained)");}};
			show("Stored instance baseline",pending.entry.baseline);show("Current source value to adopt",pending.sourceValue);show("Current instance value",pending.entry.instance);
			if(removal || restoration){
				const auto& dependencies=restoration?pending.restorations:pending.removals;
				constexpr int PageSize=16;const int pages=static_cast<int>((dependencies.size()+PageSize-1)/PageSize);
				pending.removalPage=std::clamp(pending.removalPage,0,(std::max)(0,pages-1));
				ImGui::Text("Complete dependency review: %zu rows; next selection: %zu",dependencies.size(),pending.nextSelection.Ids().size());
				ImGui::BeginDisabled(pending.removalPage==0);if(ImGui::Button("Previous removal page"))--pending.removalPage;ImGui::EndDisabled();ImGui::SameLine();
				ImGui::BeginDisabled(pending.removalPage+1>=pages);if(ImGui::Button("Next removal page"))++pending.removalPage;ImGui::EndDisabled();ImGui::SameLine();ImGui::Text("%d / %d",pending.removalPage+1,pages);
				ImGui::BeginChild("Removal closure",ImVec2(680,240),true);
				for(size_t index=size_t(pending.removalPage)*PageSize;index<dependencies.size() && index<size_t(pending.removalPage+1)*PageSize;++index){const auto& row=dependencies[index];const auto bytes=Engine::Editor::PrefabOverrideDisplayPrefix(row.name,256);ImGui::PushID(static_cast<int>(index));
					EntityId sceneId=row.scene;if(restoration)for(const auto& pair:pending.nextWorld.Find(pending.root)->prefabLink["mapping"].get<crude_json::array>())if(pair["local"].get<crude_json::number>()==double(row.local)){sceneId=EntityId(pair["entity"].get<crude_json::number>());break;}
					if(ImGui::TreeNode("removal","%s | local %llu / Scene %llu | %.*s",row.change==Engine::Editor::PrefabThreeWayChange::Conflict?"CONFLICT":restoration?row.baseline?"Restore mapped Entity":"Add required source parent":row.instance?"Remove Entity":"Baseline tombstone only",static_cast<unsigned long long>(row.local),static_cast<unsigned long long>(sceneId),static_cast<int>(bytes),row.name.c_str())){show("Stored baseline",row.baseline);show("Current source",row.source);show("Current instance",row.instance);ImGui::TreePop();}ImGui::PopID();
				}ImGui::EndChild();
			}
			ImGui::TextUnformatted(pending.comparison==Engine::Editor::PrefabThreeWayChange::Conflict?"Conflict: source and instance changed differently":pending.comparison==Engine::Editor::PrefabThreeWayChange::Converged?"Converged: source and instance have the same changed value":pending.comparison==Engine::Editor::PrefabThreeWayChange::InstanceOnly?"Only the instance changed":pending.comparison==Engine::Editor::PrefabThreeWayChange::SourceOnly?"Only the source changed":"Unchanged");
			const auto& dependencies=restoration?pending.restorations:pending.removals;
			const bool conflict=(removal || restoration)?std::any_of(dependencies.begin(),dependencies.end(),[](const auto& row){return row.change==Engine::Editor::PrefabThreeWayChange::Conflict;}):pending.comparison==Engine::Editor::PrefabThreeWayChange::Conflict;
			if(conflict)ImGui::Checkbox(restoration?"Adopt ALL changed source values in these deleted Entities/parents":removal?"Discard ALL conflicting edits/moved Entities in this removal closure":"Replace the conflicting instance field with the current source value",&pending.conflictApproved);
			ImGui::BeginDisabled(conflict && !pending.conflictApproved);
			if(ImGui::Button(restoration?"Confirm reviewed Entity restoration":removal?"Confirm reviewed Entity removal":addition?"Confirm source Entity addition":adoption?"Confirm source field adoption":"Confirm field Revert")){const bool approved=pending.conflictApproved;std::string error;const bool accepted=restoration?ConfirmEditorPrefabEntityRestoration(error,approved):removal?ConfirmEditorPrefabEntityRemoval(error,approved):addition?ConfirmEditorPrefabEntityAddition(error):ConfirmEditorPrefabFieldRevert(error,approved);if(!accepted && adoption)prefabThreeWayStatus_="Source adoption confirmation rejected: "+error;ImGui::CloseCurrentPopup();}
			ImGui::EndDisabled();
			ImGui::SameLine();if(ImGui::Button(restoration?"Cancel Entity restoration":removal?"Cancel Entity removal":addition?"Cancel source Entity addition":adoption?"Cancel source field adoption":"Cancel field Revert")){CancelEditorPrefabFieldRevert();ImGui::CloseCurrentPopup();}
		}
		ImGui::EndPopup();
	}
#endif
}

bool Game::ApplyEditorHistory(bool redo,std::string& error)
{
	if(!RefreshHistoryRecovery(error) || prefabRecoveryBlocked_){
		if(error.empty())error="Unresolved Prefab source transaction; inspect Prefab Recovery before replay";
		genericWorldStatus_=error;return false;
	}
	if(!editorMode_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() ||
		assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
		worldPlayback_.GetState()!=WorldPlayback::State::Editing) {
		error="History requires an editing Scene without a pending document, drag, import or export";genericWorldStatus_=error;return false;
	}
	const bool applied = redo ? entityHistory_.Redo(editorWorld_, editorSelection_, error,&documentState_)
		: entityHistory_.Undo(editorWorld_, editorSelection_, error,&documentState_);
	std::string diagnostic;const bool scanned=RefreshHistoryRecovery(diagnostic);
	if (!applied) { genericWorldStatus_ = error;if(prefabRecoveryBlocked_)genericWorldStatus_+="; Prefab Recovery blocks further authoring";return false; }
	selectedEditorEntity_=editorSelection_.Primary();
	physicsWorld_.ResetSimulation();
	genericPlayerEntity_ = 0;
	for (const auto& entity : editorWorld_.GetEntities()) {
		if (entity.name == "Player") { genericPlayerEntity_ = entity.id; break; }
	}
	if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene())) scene->ResetAnimations();
	genericWorldStatus_ = redo ? "Entity edit redone" : "Entity edit undone";
	if(prefabRecoveryBlocked_)genericWorldStatus_+="; retained pending data blocks further authoring";
	if(!scanned)genericWorldStatus_+="; inventory: "+diagnostic;
	error.clear();return true;
}
bool Game::PlayEditorScene(std::string& error)
{
	try {
		if(!RefreshHistoryRecovery(error) || prefabRecoveryBlocked_)throw std::runtime_error(error.empty()?"Resolve pending Prefab recovery before Play":error);
		if(!editorMode_ || !genericWorldReady_ || gizmoDragActive_ || documentState_.HasPendingAction() || assetImportJob_.IsBusy() ||
			exportJob_.GetState()==Engine::Services::ExportJob::State::Running)throw std::runtime_error("Play requires an idle Editor without document, drag, import or export operations");
		if(worldPlayback_.GetState()==WorldPlayback::State::Playing)throw std::runtime_error("Editor is already Playing");
		const bool starting=worldPlayback_.GetState()==WorldPlayback::State::Editing;
		auto settled=entityHistory_;settled.Commit();
		if(!worldPlayback_.Play(editorWorld_))throw std::runtime_error("Editor playback could not start");
		entityHistory_.Swap(settled);if(starting)authoredSelection_=selectedEditorEntity_;
		error.clear();return true;
	}catch(const std::exception& exception){error=exception.what();genericWorldStatus_="Play rejected: "+error;return false;}
}

bool Game::StopEditorPlayback(std::string& error)
{
	if (!worldPlayback_.Stop(editorWorld_,scriptRuntime_,physicsWorld_,error)) return false;
	if (runtimeSceneChanged_) {
		if (auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))
			scene->ResetWorldRenderState();
		runtimeSceneChanged_ = false;
	}
	return true;
}

void Game::SynchronizeGenericPlayer()
{
	if (const auto* playerEntity = editorWorld_.Find(genericPlayerEntity_)) {
		if (auto* scene = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene())) {
			EulerTransform transform = scene->GetPlayerTransform();
			transform.translate = playerEntity->transform.position;
			transform.rotate = playerEntity->transform.rotation;
			transform.scale = playerEntity->transform.scale;
			scene->SetPlayerTransform(transform);
		}
	}
}

}
