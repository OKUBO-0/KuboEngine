#include "Game.h"
#include "GamePlayScene.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "../entity/EntityScene.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "ImGuiManager.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "engine/editor/ProjectPreviewCapture.h"
#include "engine/editor/DebugEditorManager.h"
#include "engine/base/ProjectPreviewPath.h"
#include <dwmapi.h>
#include <algorithm>

#pragma comment(lib, "Dwmapi.lib")

namespace Engine::Scene {

Game::Game() : initialSceneName_("GAMEPLAY")
{
}

Game::Game(std::unique_ptr<AbstractSceneFactory> sceneFactory, std::string initialSceneName)
    : initialSceneName_(std::move(initialSceneName))
{
    SetSceneFactory(std::move(sceneFactory));
}

Game::~Game() = default;

void Game::ResetEditorGridDefault()
{
    const auto& entities = editorWorld_.GetEntities();
    const bool canvas = std::any_of(entities.begin(),entities.end(),[](const auto& entity) {
        return entity.active && entity.hasCanvas && entity.canvas.enabled;
    });
    const bool model = std::any_of(entities.begin(),entities.end(),[](const auto& entity) {
        return entity.active && entity.hasModel;
    });
    editorGridVisible_ = !canvas || model;
}

void Game::ConfigureEditorMenuCommands(Editor::DebugEditorMenuConfig& menu)
{
    menu.onResetLayout = [this] { imGuiManager->RequestDefaultDockLayout(); };
    menu.sceneEditUnavailableReason = [this](Editor::SceneEditAction action) {
        return SceneEditUnavailableReason(action);
    };
    menu.onSceneEdit = [this](Editor::SceneEditAction action) { RunSceneEditAction(action); };
    using Display = Editor::DebugPlaybackState;
    const auto* busy = EditorDocumentBusyReason();
    if (!genericSceneSelected_) {
        auto* sample = dynamic_cast<GamePlayScene*>(SceneManager::GetInstance()->GetCurrentScene());
        if (!sample) return;
        using SampleState = GamePlayScene::Playback;
        const auto state = sample->GetPlayback();
        menu.playbackState = state == SampleState::Playing ? Display::Playing :
            state == SampleState::Paused ? Display::Paused : Display::Stopped;
        menu.saveLabel = "Save Scene";
        menu.saveUnavailableReason = busy ? busy : "Stop playback before saving the Scene.";
        if (busy) return;
        if (state == SampleState::Editing) {
            menu.onSave = [this,sample] { sample->SaveCurrentScene(); genericWorldStatus_ = sample->GetSceneFileStatus(); };
        }
        if (state != SampleState::Playing) menu.onPlay = [sample] { sample->Play(); };
        if (state == SampleState::Playing) menu.onPause = [sample] { sample->Pause(); };
        if (state != SampleState::Editing) menu.onStop = [sample] { sample->Stop(); };
        return;
    }
    if (!genericWorldReady_) return;
    using State = WorldPlayback::State;
    const auto state = worldPlayback_.GetState();
    menu.playbackState = state == State::Playing ? Display::Playing :
        state == State::Paused ? Display::Paused : Display::Stopped;
    menu.saveLabel = "Save Scene";
    menu.saveUnavailableReason = busy ? busy : "Stop playback before saving the Scene.";
    if (busy) return;
    if (state == State::Editing) {
        menu.onSave = [this] { SaveGenericDocument(); };
    }
    if (state != State::Playing) menu.onPlay = [this] { std::string error; PlayEditorScene(error); };
    if (state == State::Playing) menu.onPause = [this] { worldPlayback_.Pause(); };
    if (state != State::Editing) menu.onStop = [this] {
        std::string error;
        if (!worldPlayback_.Stop(editorWorld_,scriptRuntime_,physicsWorld_,error)) {
            if (!error.empty()) genericWorldStatus_ = error;
            return;
        }
        selectedEditorEntity_ = editorWorld_.Find(authoredSelection_) ? authoredSelection_ : 0;
        editorSelection_.Synchronize(editorWorld_,selectedEditorEntity_);
        if (auto* scene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene()))
            scene->ResetAnimations();
    };
}

const char* Game::EditorDocumentBusyReason() const
{
    if (prefabRecoveryBlocked_) return "Resolve pending Prefab Recovery first.";
    if (controllerClosePending_ || sampleClosePending_ || documentState_.HasPendingAction()) return "Finish the pending document operation first.";
    if (gizmoDragActive_) return "Finish the active transform first.";
    if (assetImportJob_.IsBusy()) return "Wait for the asset import to finish.";
    if (exportJob_.GetState() == Services::ExportJob::State::Running) return "Wait for the export to finish.";
    return nullptr;
}

const char* Game::SceneEditUnavailableReason(Editor::SceneEditAction action) const
{
    using Action = Editor::SceneEditAction;
    const bool selectionAction = action == Action::Duplicate || action == Action::Delete;
    if (!editorMode_ || !genericWorldReady_ || (selectionAction && !genericSceneSelected_))
        return "Open an editable Scene.";
    if (worldPlayback_.GetState() != WorldPlayback::State::Editing) return "Stop playback to edit the Scene.";
    if (const auto* busy = EditorDocumentBusyReason()) return busy;
    if (action == Action::Undo && !entityHistory_.CanUndo()) return "No Scene edit to undo.";
    if (action == Action::Redo && !entityHistory_.CanRedo()) return "No Scene edit to redo.";
    if (selectionAction && !editorWorld_.Find(selectedEditorEntity_)) return "Select an Entity in the Scene.";
    if (selectionAction && HasHierarchyHiddenSelection()) return "The selection includes Entities hidden by Hierarchy search. Clear the search or select visible Entities.";
    return nullptr;
}

bool Game::MatchesHierarchySearch(std::string_view name) const
{
    const std::string_view query(hierarchySearch_);
    if (query.empty()) return true;
    const auto foldAscii = [](unsigned char value) {
        return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : int(value);
    };
    return std::search(name.begin(),name.end(),query.begin(),query.end(),[&](char a,char b) {
        return foldAscii(a) == foldAscii(b);
    }) != name.end();
}

bool Game::HasHierarchyHiddenSelection() const
{
    if (!genericHierarchyOpen_ || !hierarchySearch_[0] || editorSelection_.Ids().empty()) return false;
    for (const auto& entity : editorWorld_.GetEntities()) {
        if (editorSelection_.Contains(entity.id) && !MatchesHierarchySearch(entity.name)) return true;
    }
    return false;
}

bool Game::RunSceneEditAction(Editor::SceneEditAction action)
{
    if (SceneEditUnavailableReason(action)) return false;
    std::string error;
    using Action = Editor::SceneEditAction;
    switch (action) {
    case Action::Undo: return ApplyEditorHistory(false,error);
    case Action::Redo: return ApplyEditorHistory(true,error);
    case Action::Duplicate: return DuplicateEditorSelection(error);
    case Action::Delete: return DeleteEditorSelection(error);
    }
    return false;
}

void Game::ConfigureHubWindowStartup()
{
    if (!editorMode_ || !hubReadyEvent_) return;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);
    hubShowAfterFirstFrame_ = !(startup.dwFlags & STARTF_USESHOWWINDOW) || startup.wShowWindow != SW_HIDE;
    winApp->DeferInitialShow();
}

void Game::FinishHubWindow()
{
    if (!editorMode_) return;
    if (genericSceneSelected_) {
        debugAssetBrowserOpen_ = true;
        debugConsoleOpen_ = true;
        debugKeyInputOpen_ = debugStatsOpen_ = debugOffscreenOpen_ = false;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(winApp->GetHwnd(),20,&dark,sizeof(dark));
    hubPreview_ = std::make_unique<Editor::ProjectPreviewCapture>();
}

void Game::RetireHubPreview()
{
    if (hubPreview_ && dxCommon) hubPreview_->RetireAfterGpuIdle(*dxCommon);
}

void Game::CaptureHubPreview()
{
    if (!hubPreview_ || projectFile_.empty() || documentState_.IsDirty() ||
        documentState_.Path().empty() || worldPlayback_.GetState() != WorldPlayback::State::Editing) return;
    const auto now = GetTickCount64();
    if (!hubPreviewInitialAt_) hubPreviewInitialAt_ = now + 2000;
    if (hubPreviewInitialCheck_ && now >= hubPreviewInitialAt_) {
        hubPreviewInitialCheck_ = false;
        std::error_code error;
        if (Base::PreviewDirectoriesAllowed(GetProjectRoot(),true) &&
            !std::filesystem::is_regular_file(Base::ProjectPreviewPath(GetProjectRoot()),error))
            hubPreviewRequested_ = true;
    }
    if (hubPreviewCloseFrame_ || (hubPreviewRequested_ && now >= hubPreviewNextAt_)) {
        hubPreviewNextAt_ = now + 15000;
        offscreenRenderManager->EndEditorView();
        if (hubPreview_->Record(*dxCommon,*offscreenRenderManager,GetProjectRoot()))
            hubPreviewRequested_ = false;
        offscreenRenderManager->BindEditorView();
    }
}

void Game::NotifyHubAfterFrame()
{
    if (hubPreviewCloseFrame_) endRequest_ = true;
    if (!hubReadyEvent_) return;
    if (hubShowAfterFirstFrame_) {
        hubShowAfterFirstFrame_ = false;
        ShowWindow(winApp->GetHwnd(),SW_SHOW);
        return;
    }
    SetEvent(hubReadyEvent_);
    hubReadyEvent_ = nullptr;
}

void Game::Draw()
{
    if (hubPreview_) hubPreview_->Poll(*dxCommon);
    dxCommon->PrepareForFrame();
    dxCommon->BeginGpuFrameTiming();
    if (editorMode_) assetThumbnails_.DrawPrepared();
    offscreenRenderManager->Begin();
    srvManager->PreDraw();
    auto* genericScene = dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene());
    if (genericScene) genericScene->SetUIOutputExtent(GetGameOutputExtent());
    if (editorMode_ && genericScene && offscreenRenderManager->HasEditorView())
        genericScene->DrawGameView(GetGameOutputExtent().Aspect());
    else SceneManager::GetInstance()->Draw();
    offscreenRenderManager->End();
    if (editorMode_ && genericScene && offscreenRenderManager->HasEditorView()) {
        offscreenRenderManager->BeginEditorView();
        genericScene->SetUIOutputExtent(offscreenRenderManager->GetEditorViewExtent());
        genericScene->Draw();
        CaptureHubPreview();
        if (debugSceneViewOpen_ && editorViewEnabled_ && editorGridVisible_)
            offscreenRenderManager->DrawEditorGrid(editorViewCamera_.GetViewProjectionMatrix().m);
        offscreenRenderManager->EndEditorView();
    }
    dxCommon->Begin();
    offscreenRenderManager->Draw();
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
    if (editorMode_) imGuiManager->Draw();
#endif
    dxCommon->End();
    NotifyHubAfterFrame();
}

} // namespace Engine::Scene
