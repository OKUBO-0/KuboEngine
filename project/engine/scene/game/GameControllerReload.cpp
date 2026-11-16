#include "Game.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/core/SceneManager.h"

namespace Engine::Scene {
bool Game::ReloadSelectedController(std::string& error){
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing ||
       documentState_.HasPendingAction() || controllerClosePending_ || controllerEditor_.NeedsResolution() ||
       assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running ||
       prefabRecoveryBlocked_ || gizmoDragActive_){error="Controller reload requires idle Editing and a resolved Controller draft";return false;}
    const auto* source=controllerEditor_.Session().Source();const auto* entity=editorWorld_.Find(selectedEditorEntity_);
    if(!source || !entity || !entity->hasAnimator || entity->animator.controllerGuid!=source->guid){
        error="Selected Entity must already use this saved Controller GUID; reload never reassigns it";return false;
    }
    auto* scene=dynamic_cast<EntityScene*>(SceneManager::GetInstance()->GetCurrentScene());
    if(!scene || scene->OwnerWorld()!=&editorWorld_){error="Selected Controller owner is not in the active Editor Scene";return false;}
    return scene->ReloadController(selectedEditorEntity_,error,source);
}
}
