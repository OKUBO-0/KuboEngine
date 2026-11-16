#include "Game.h"
#include "ImGuiManager.h"
#include "SrvManager.h"

namespace Engine::Scene {
void Game::PrepareAssetThumbnails(){
    if(!imGuiManager||!dxCommon||!srvManager)return;
    Editor::ThumbnailGuiSlots slots;
    slots.createTextureSrv=[this](const Base::TextureLease& lease){return imGuiManager->CreateExternalTextureSrv(lease);};
    slots.createModelSrv=[this](ID3D12Resource* image){return imGuiManager->CreateExternalTextureSrv(image);};
    slots.release=[this](uint64_t texture){imGuiManager->ReleaseExternalTextureAfterGpuIdle(texture);};
    assetThumbnails_.BeforeUi(dxCommon,srvManager.get(),std::move(slots),GetProjectRoot(),projectFile_,projectAssets_);
}
}
