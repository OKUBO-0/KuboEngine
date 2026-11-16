#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Framework.h"
#include "application_check_mode.h"
#include "runtime_scene_transition_fixture.h"
#include "animator_host_fixture.h"
#include "runtime_prefab_host_fixture.h"
#include "startup_close_editor_fixture.h"
#include "authored_lighting_fixture.h"
#include "sprite_move_render_fixture.h"
#include "particle_group_fixture.h"
#include "particle_authoring_fixture.h"
#include "asset_thumbnail_fixture.h"
#include "game_render_inspector_checks.h"
#include "git_fixture.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "DebugEditorManager.h"
#include "ImGuiManager.h"
#include "engine/input/Input.h"
#include "Logger.h"
#include "Game.h"
#include "GamePlayScene.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "engine/editor/RenderComponentInspector.h"
#include "DebugEditorManager.h"
#include "engine/editor/EditorSettingsFile.h"
#include "engine/editor/NativeBuildSettings.h"
#include "engine/editor/ProjectPrefab.h"
#include "engine/editor/PrefabEntityAddition.h"
#include "engine/editor/PrefabEntityRemoval.h"
#include "engine/editor/PrefabEntityRestoration.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <crtdbg.h>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <atomic>
#include <array>
#include <chrono>
#include <unordered_map>
#include "engine/base/AssetPath.h"
#include "engine/scene/entity/EntityScene.h"
#include "CameraManager.h"
#include "OffscreenRenderManager.h"
#include "ImGuizmoManager.h"
#include "engine/editor/GizmoSnapSettings.h"

void CheckShaderCache();
void CheckGizmoInput();
void CheckSceneEditing(Engine::Scene::GamePlayScene& scene);

#if defined(KUBO_APPLICATION_CHECKS)
namespace Engine::Editor {
class ControllerCanvasChecksAccess {
public:
    static void Position(AnimatorControllerPanel& panel,bool set,bool remember=false,float x=752,float y=-144){
        if(!panel.context_)throw std::runtime_error("Canvas probe has no context");
        const auto key=AnimatorControllerCanvasState::Key(AnimatorControllerCanvasState::Kind::StateNode,panel.session_.Document()->AuthoringIds().layers[0].states[0]);
        AnimatorControllerPanel::Current current(panel.context_);
        if(set){ControllerNodes::SetNodePosition(key,{x,y});if(remember)panel.canvasState_.Store(key,{x,y});}
        else {const auto p=ControllerNodes::GetNodePosition(key);const auto saved=panel.canvasState_.Position(key,{});
            if(p.x!=x || p.y!=y || saved.x!=x || saved.y!=y)throw std::runtime_error("Actual panel lost stable node position across revision/history");}
    }
    static bool Pending(const AnimatorControllerPanel& panel){return panel.layoutAuto_.Pending();}
};
}
namespace Engine::Scene {
// Optional runner only: normal builds have no friend or fixture entry point.
class GameApplicationChecksAccess {
public:
    static auto& Scripts(Game& game){return game.scriptRuntime_;}
    static const auto& EntityAudio(const Game& game){return game.entityAudio_;}
    static const auto& ScriptAudio(const Game& game){return game.scriptAudio_;}
    static auto& Entities(Game& game){return game.editorWorld_;}
    static void OpenAnimatorInspector(Game& game){game.genericInspectorOpen_=true;}
    static auto& ControllerPanel(Game& game){return game.controllerEditor_;}
    static auto& ControllerAssets(Game& game){return game.projectAssets_;}
    static size_t ControllerInventorySize(const Game& game){return game.controllerPendingEntries_.size();}
    static size_t ControllerArchiveSize(const Game& game){return game.controllerArchiveEntries_.size();}
    static uint64_t ControllerRecoveryFrames(const Game& game){return game.controllerRecoveryFrames_;}
    static bool ControllerExitPending(const Game& game){return game.controllerClosePending_;}
    static bool McpRunning(const Game& game){return game.mcpSession_.IsRunning();}
    static bool ImportBusy(const Game& game){return game.assetImportJob_.IsBusy();}
    static void HideAssetBrowser(Game& game){game.debugAssetBrowserOpen_=false;}
    static void ShowAssetBrowser(Game& game) { game.debugAssetBrowserOpen_ = true; }
    static void SetSceneOverlays(Game& game, bool grid, bool gizmo) {
        game.editorGridVisible_ = grid;
        game.gizmoEnabled_ = gizmo;
    }
    static void SetSceneGuides(Game& game, bool enabled) {
        game.editorSelectionGuides_ = enabled;
        game.editorComponentGuides_ = enabled;
    }
    static void SetFrontOrthographicView(Game& game) {
        game.editorCameraState_.position={0,0,-10};
        game.editorCameraState_.rotation={0,0,0};
        game.editorCameraState_.orthographic=true;
    }
    static const std::string& EditorStatus(const Game& game) {return game.genericWorldStatus_;}
    static bool CreateLuaScriptAsset(Game& game,const std::filesystem::path& path,std::string& error) {
        return game.CreateLuaScriptAsset(path,error);
    }
    static void FocusSceneSelection(Game& game) { game.FocusEditorSelection(); }
    static Vector3 SceneCameraPosition(const Game& game) { return game.editorCameraState_.position; }
    static bool GameViewportReceivesInput(const Game& game) { return game.gameScriptInputAllowed_; }
    static bool StartShutdownImport(Game& game,const std::filesystem::path& root){Assets::AssetImportJob::Request request;request.projectRoot=root;request.assetRoot=root/"Assets";return game.assetImportJob_.Start(std::move(request));}
    static bool StartFileImport(Game& game,const std::filesystem::path& source) {
        Assets::AssetImportJob::Request request{game.GetProjectRoot(),game.projectAssetRoot_,{},false,game.projectFile_};
        request.sourceFiles.push_back(source);
        return game.assetImportJob_.Start(std::move(request));
    }
    static bool RecordSourceOnly(Game& game,std::shared_ptr<Editor::EntityHistoryEffect> effect,std::string& error){
        game.editorSelection_.Synchronize(game.editorWorld_,game.selectedEditorEntity_);
        const auto selected=game.editorSelection_.Capture();
        if(!game.entityHistory_.Record(game.editorWorld_.Capture(),game.editorWorld_,selected,selected,error,std::move(effect)))return false;
        game.entityHistory_.Commit();return true;
    }
    static std::string World(const Game& game){return game.editorWorld_.Capture().dump();}
    static EntityId HighWater(const Game& game){return game.editorWorld_.AllocationHighWatermark();}
    static Editor::EntityDocumentState Document(const Game& game){return game.documentState_;}
    static void ReplaceHistory(Game& game,Editor::EntityHistory history){game.entityHistory_.Swap(history);}
    static bool StructuralPreviewMatchesCore(const Game& game,std::string& error){
        if(!game.prefabFieldRevert_ || !game.prefabFieldRevert_->adoption){error="No structural preview";return false;}
        const auto& pending=*game.prefabFieldRevert_;auto world=game.editorWorld_;auto selection=game.editorSelection_;auto history=game.entityHistory_;auto document=game.documentState_;
        const bool prepared=pending.entityRestoration?Editor::AdoptPrefabEntityRestoration(world,pending.root,pending.revision.definition,pending.revision.guid,*pending.adoption,selection,history,error,&document,true):pending.entityRemoval?Editor::AdoptPrefabEntityRemoval(world,pending.root,pending.revision.definition,pending.revision.guid,*pending.adoption,selection,history,error,&document,true):pending.entityAddition?Editor::AdoptPrefabEntityAddition(world,pending.root,pending.revision.definition,pending.revision.guid,*pending.adoption,selection,history,error,&document):false;
        return prepared && world.Capture().dump()==pending.nextWorld.Capture().dump() && world.AllocationHighWatermark()==pending.nextWorld.AllocationHighWatermark() && selection.Capture()==pending.nextSelection.Capture() && history.HasSameState(pending.nextHistory) && document.IsDirty()==pending.nextDirty;
    }
    static bool RenameRemovalFixture(Game& game,EntityId child,std::string& error){
        const auto before=game.editorWorld_.Capture();const auto selection=game.editorSelection_.Capture();game.editorWorld_.Find(child)->name="Independent removal child edit";
        if(!game.entityHistory_.Record(before,game.editorWorld_,selection,selection,error))return false;
        game.entityHistory_.Commit();game.documentState_.Observe(game.editorWorld_);return true;
    }
    static bool Editing(const Game& game){return game.worldPlayback_.GetState()==WorldPlayback::State::Editing;}
    static Physics::PhysicsAdvanceUsage PhysicsUsage(const Game& game){return game.physicsWorld_.GetAdvanceUsage();}
    static InputSystem::FixedInputUsage FixedInputUsage(const Game& game){return game.scriptRuntime_.GetFixedInputUsage();}
    static void QueueInput(Game& game,InputSystem::InputFrame frame){game.scriptRuntime_.SetInputFrame(std::move(frame));}
    static void Pause(Game& game){game.worldPlayback_.Pause();}
    static void StopForDocumentClose(Game& game){game.RequestDocumentAction(Editor::EntityDocumentState::Action::Close);}
    static bool GitIndexBusy(const Game& game){return game.gitIndexRequest_.valid();}
    static const std::string& GitIndexMessage(const Game& game){return game.gitIndexMessage_;}
    static bool GitDiffBusy(const Game& game){return game.gitDiffRequest_.valid();}
    static const std::string& GitDiffText(const Game& game){return game.gitDiff_.display;}
    static const std::string& GitDiffError(const Game& game){return game.gitDiff_.error;}
    static bool GitHistoryBusy(const Game& game){return game.gitHistoryRequest_.valid();}
    static const Services::GitHistory& GitHistory(const Game& game){return game.gitHistory_.history;}
    static const std::string& GitHistoryError(const Game& game){return game.gitHistory_.error;}
    static void GitDirty(Game& game,bool dirty){game.editorWorld_.Find(1)->name=dirty ? "Unsaved Git probe" : "Git probe";game.documentState_.Observe(game.editorWorld_);}
    static bool StartGuardExport(Game& game,const std::filesystem::path& root,std::string& error){return game.exportJob_.Start(root/"guard.kubo",root/"Export",root/"guard.ps1",root/"guard.ps1",error);}
    static bool ExportRunning(const Game& game){return game.exportJob_.GetState()==Services::ExportJob::State::Running;}
    static bool StopGuardExport(Game& game){std::string error;const bool cancelled=game.exportJob_.Cancel(error);const auto deadline=GetTickCount64()+5000;while(game.exportJob_.GetState()==Services::ExportJob::State::Running && GetTickCount64()<deadline){game.exportJob_.Poll();Sleep(1);}return cancelled && game.exportJob_.GetState()==Services::ExportJob::State::Cancelled;}
    static void ForgetHistory(Game& game){game.entityHistory_.Clear();}
    static bool RenameForShortcut(Game& game,EntityId id,std::string& error){
        const auto before=game.editorWorld_.Capture();const auto selected=game.editorSelection_.Capture();
        auto* entity=game.editorWorld_.Find(id);if(!entity){error="Shortcut fixture entity missing";return false;}entity->name+=" shortcut";
        if(!game.entityHistory_.Record(before,game.editorWorld_,selected,selected,error))return false;
        game.entityHistory_.Commit();game.documentState_.Observe(game.editorWorld_);return true;
    }
    static Editor::EntityHistory History(const Game& game){return game.entityHistory_;}
    static void SetThreeWayView(Game& game,int filter,int page){game.prefabThreeWayFilter_=filter;game.prefabThreeWayPage_=page;}
    static std::pair<int,int> ThreeWayView(const Game& game){return {game.prefabThreeWayFilter_,game.prefabThreeWayPage_};}
    static bool ComparisonSessionMatches(const Game& game){return game.prefabThreeWay_ && game.prefabThreeWay_->scene==game.documentState_.Path() && game.prefabThreeWay_->generation==game.documentState_.Generation();}
    static bool OverrideSessionMatches(const Game& game){return game.prefabOverridesScene_==game.documentState_.Path() && game.prefabOverridesGeneration_==game.documentState_.Generation();}
    static bool RenameFieldFixture(Game& game,EntityId first,EntityId second,std::string& error){
        const auto before=game.editorWorld_.Capture();const auto selection=game.editorSelection_.Capture();
        game.editorWorld_.Find(first)->name="Elected unselected name";game.editorWorld_.Find(second)->name="Other instance-owned name";
        if(!game.entityHistory_.Record(before,game.editorWorld_,selection,selection,error))return false;
        game.entityHistory_.Commit();game.documentState_.Observe(game.editorWorld_);return true;
    }
    static bool Stop(Game& game,std::string& error){return game.StopEditorPlayback(error);}
};
}
#endif

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class ResourceChecks final : public Engine::Base::Framework {
public:
    void Draw() override {}

    void Check()
    {
#ifdef _DEBUG
        Engine::Base::Logger::Log("Console integration check", Engine::Base::Logger::Level::Warning);
        bool consoleOpen = true;
        int sceneToolbarCalls = 0;
        for (int frame = 0; frame < 3; ++frame) {
            imGuiManager->Begin();
            Engine::Editor::DebugEditorManager::DrawConsole(&consoleOpen);
            bool sceneViewOpen = true;
            Engine::Editor::DebugEditorManager::DrawSceneViewport(&sceneViewOpen, [&] { ++sceneToolbarCalls; ImGui::TextUnformatted("Editor camera toolbar test"); });
            imGuiManager->End();
        }
        Require(ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0,
            "Console must produce visible ImGui geometry");
        Require(sceneToolbarCalls > 0, "Scene viewport did not invoke its in-window toolbar");
#endif
        namespace fs = std::filesystem;
        auto* textures = Engine::Base::TextureManager::GetInstance();
        const std::string white = "Resources/textures/common/white.png";
        textures->LoadTexture(white);
        const auto whiteIndex = textures->GetTextureIndexByFilePath(white);
        const auto used = srvManager->GetUsedCount();
        const auto absolute = fs::absolute(white).generic_string();
        textures->LoadTextures({white, absolute, "./Resources/textures/common/../common/white.png"});
        Require(textures->GetTextureIndexByFilePath(absolute) == whiteIndex, "Texture aliases must reuse SRV");
        Require(srvManager->GetUsedCount() == used, "Texture aliases must not allocate descriptors");
        const uint32_t pixel = 0xffffffff;
        textures->LoadTextureFromRGBA("test://opaque/../pixel", 1, 1, &pixel, sizeof(pixel), false);
        const auto memoryIndex = textures->GetTextureIndexByFilePath("test://opaque/../pixel");
        textures->LoadTextures({"test://opaque/../pixel"});
        Require(textures->GetTextureIndexByFilePath("test://opaque/../pixel") == memoryIndex,
            "Memory texture names must remain opaque");

        using Engine::Base::TextureLoadOptions;
        using Engine::Base::TextureColorSpace;
        using Engine::Base::TextureMipMode;
        const TextureLoadOptions linear{TextureColorSpace::Linear, TextureMipMode::Preserve};
        const TextureLoadOptions srgb{TextureColorSpace::Srgb, TextureMipMode::Preserve};
        const TextureLoadOptions mip{TextureColorSpace::Srgb, TextureMipMode::Generate};
        const std::string checker = "Resources/textures/common/uvChecker.png";
        textures->LoadTexture(checker, linear);
        textures->LoadTexture(checker, srgb);
        textures->LoadTexture(checker, mip);
        Require(!DirectX::IsSRGB(textures->GetMetaData(checker, linear).format), "Linear format must stay linear");
        Require(DirectX::IsSRGB(textures->GetMetaData(checker, srgb).format), "sRGB format must be marked sRGB");
        Require(textures->GetMetaData(checker, linear).mipLevels == 1, "Preserve must not generate PNG mips");
        Require(textures->GetMetaData(checker, mip).mipLevels > 1, "Generate must produce mips");
        Require(textures->GetTextureIndexByFilePath(checker, linear) != textures->GetTextureIndexByFilePath(checker, srgb),
            "Different color settings must have separate SRVs");
        Require(textures->GetTextureIndexByFilePath(checker, srgb) != textures->GetTextureIndexByFilePath(checker, mip),
            "Different mip settings must have separate SRVs");
        const auto optionCount = srvManager->GetUsedCount();
        textures->LoadTextures({checker, fs::absolute(checker).generic_string()}, linear);
        Require(srvManager->GetUsedCount() == optionCount, "Same options and aliases must reuse SRVs");
        bool wrongOptions = false;
        try { textures->GetMetaData(checker); } catch (const std::out_of_range&) { wrongOptions = true; }
        Require(wrongOptions, "Getters must not silently select a different option variant");
        textures->LoadTextures({white, fs::absolute(white).generic_string()}, linear);
        Require(srvManager->GetUsedCount() == optionCount + 1, "New batch variants must deduplicate aliases");
        Engine::Graphics2D::Sprite optionSprite;
        optionSprite.Initialize(Engine::Graphics2D::SpriteCommon::GetInstance(), checker, linear);
        optionSprite.Update();
        optionSprite.SetTexture(checker, mip);
        optionSprite.Update();
        wrongOptions = false;
        try { textures->LoadTexture("test://opaque/../pixel", linear); }
        catch (const std::invalid_argument&) { wrongOptions = true; }
        Require(wrongOptions, "File options must not reinterpret opaque memory assets");
        const TextureLoadOptions authored{TextureColorSpace::Automatic, TextureMipMode::Preserve};
        const std::string dds = "Resources/textures/skybox/test.dds";
        DirectX::TexMetadata authoredMetadata{};
        Require(SUCCEEDED(DirectX::GetMetadataFromDDSFile(fs::path(dds).c_str(), DirectX::DDS_FLAGS_NONE, authoredMetadata)),
            "DDS test metadata must load");
        textures->LoadTexture(dds, authored);
        Require(textures->GetMetaData(dds, authored).mipLevels == authoredMetadata.mipLevels &&
            textures->GetMetaData(dds, authored).format == authoredMetadata.format,
            "Preserve must retain DDS format and authored mips");
        if (DirectX::IsCompressed(authoredMetadata.format)) {
            wrongOptions = false;
            const auto beforeRejected = srvManager->GetUsedCount();
            try { textures->LoadTexture(dds, {TextureColorSpace::Automatic, TextureMipMode::Generate}); }
            catch (const std::invalid_argument&) { wrongOptions = true; }
            Require(wrongOptions && beforeRejected == srvManager->GetUsedCount(),
                "Unsupported compressed mip generation must fail before descriptor allocation");
        }
        std::cout << "PASS: texture color/mip variants, batch reuse, Sprite options and authored DDS\n";

        auto* models = Engine::Graphics3D::ModelManager::GetInstance();
        // Real GPU revision resources: old models/textures remain manager-owned.
        const auto revisionFixture = fs::temp_directory_path() / (L"kubo-gpu-revision-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        struct RevisionFixture { fs::path path; ~RevisionFixture() { std::error_code ec; fs::remove_all(path, ec); } } revisionCleanup{revisionFixture};
        fs::create_directories(revisionFixture / L"日本語");
        const auto revisionRoot = revisionFixture / L"日本語";
        const auto revisionTexture = revisionRoot / "color.dds";
        const auto saveTexture = [&](size_t width) {
            DirectX::ScratchImage image;
            Require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, width, 1, 1)), "Revision texture fixture allocation failed");
            std::memset(image.GetPixels(), 255, image.GetPixelsSize());
            Require(SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, revisionTexture.c_str())), "Revision DDS fixture write failed");
        };
        const auto saveModel = [&](bool expanded) {
            std::ofstream material(revisionRoot / "material.mtl"); material << "newmtl Sample\nmap_Kd color.dds\n";
            std::ofstream model(revisionRoot / "model.obj");
            model << "mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nusemtl Sample\nf 1//1 2//1 3//1\n";
            if (expanded) model << "v 2 0 0\nv 3 0 0\nv 2 1 0\nf 4//1 5//1 6//1\n";
            Require(bool(model) && bool(material), "Revision model fixture write failed");
        };
        const std::string revisionA(64, 'a'), revisionB(64, 'b');
        const auto nativeRoot = Engine::Base::PathToUtf8(revisionRoot);
        saveTexture(2); saveModel(false);
        auto* firstRevision = models->GetOrLoadModel("model.obj", nativeRoot, "test-guid", revisionA);
        const auto firstMaterial = firstRevision->GetModelData().submeshes.front().materialIndex;
        const auto firstTexture = firstRevision->GetModelData().materials.at(firstMaterial).textureFilePath;
        const auto firstIndex = textures->GetTextureIndexByFilePath(firstTexture);
        Require(firstRevision->GetModelData().vertices.size() == 3 && textures->GetMetaData(firstTexture).width == 2, "Initial revision GPU fixture failed");
        saveTexture(4); saveModel(true);
        auto* nextRevision = models->GetOrLoadModel("model.obj", nativeRoot, "test-guid", revisionB);
        const auto nextMaterial = nextRevision->GetModelData().submeshes.front().materialIndex;
        const auto nextTexture = nextRevision->GetModelData().materials.at(nextMaterial).textureFilePath;
        Require(nextRevision != firstRevision && nextRevision->GetModelData().vertices.size() == 6 && firstRevision->GetModelData().vertices.size() == 3,
            "Model revision did not replace geometry or invalidate old model pointers");
        Require(textures->GetMetaData(nextTexture).width == 4 && textures->GetMetaData(firstTexture).width == 2 &&
            textures->GetTextureIndexByFilePath(nextTexture) != firstIndex, "Texture revision reused stale image/SRV or invalidated old descriptor");
        Require(models->GetOrLoadModel("model.obj", nativeRoot, "test-guid", revisionB) == nextRevision, "Unchanged GPU revision did not reuse model");
        auto liveLease = models->AcquireModel("model.obj", nativeRoot, "lease-guid", revisionA);
        auto secondLease = models->AcquireModel("model.obj", nativeRoot, "lease-guid", revisionB);
        std::weak_ptr<Engine::Graphics3D::Model> weakLease = liveLease;
        Require(models->CollectUnusedModels() == 0 && !weakLease.expired(), "Model collector deleted an active lease");
        liveLease.reset();
        Require(models->CollectUnusedModels() == 1 && weakLease.expired() && secondLease->GetModelData().vertices.size() == 6,
            "Unreferenced old revision was not reclaimed or live revision was invalidated");
        auto* pinnedLease = models->GetOrLoadModel("model.obj", nativeRoot, "lease-guid", revisionB);
        secondLease.reset();
        Require(models->CollectUnusedModels() == 0 && pinnedLease->GetModelData().vertices.size() == 6 && firstRevision->GetModelData().vertices.size() == 3,
            "Raw-pointer exposure failed to pin a revision model");
        auto factoryLease = models->AcquireModel("model.obj", nativeRoot, "factory-lease-guid", revisionB);
        std::weak_ptr<Engine::Graphics3D::Model> weakFactory = factoryLease;
        auto leasedObject = Engine::Graphics3D::Object3D::Create(Engine::Graphics3D::Object3DCommon::GetInstance(), "model.obj", nativeRoot, "factory-lease-guid", revisionB);
        factoryLease.reset();
        Require(models->CollectUnusedModels() == 0 && !weakFactory.expired(), "Factory object did not retain its model lease");
        dxCommon->WaitForAllFrames(); leasedObject.reset();
        Require(models->CollectUnusedModels() == 1 && weakFactory.expired(), "Factory model lease was not released/collected");
        std::cout << "PASS: actual GPU model leases, old-revision collection, raw-pointer pinning and Object3D lease ownership\n";
        const auto textureBaseline = textures->GetCachedTextureCount();
        const auto descriptorBaseline = srvManager->GetUsedCount();
        auto textureLease = textures->AcquireTextureRevision(Engine::Base::PathToUtf8(revisionTexture), std::string(64, 'e'));
        auto sharedTextureLease = textures->AcquireTextureRevision(Engine::Base::PathToUtf8(revisionTexture), std::string(64, 'e'));
        Require(textureLease == sharedTextureLease && textureLease->metadata.width == 4, "Texture leases did not share revision/metadata");
        std::weak_ptr<const Engine::Base::TextureLease> weakTexture = textureLease;
        const auto leasedTextureIndex = textureLease->srvIndex;
        Require(textures->CollectUnusedTextures() == 0 && srvManager->IsAllocated(leasedTextureIndex), "Collector freed a leased texture");
        textureLease.reset();
        Require(textures->CollectUnusedTextures() == 0, "Collector ignored a second texture owner");
        sharedTextureLease.reset();
        Require(textures->CollectUnusedTextures() == 1 && weakTexture.expired() && !srvManager->IsAllocated(leasedTextureIndex) &&
            textures->GetCachedTextureCount() == textureBaseline && srvManager->GetUsedCount() == descriptorBaseline,
            "Unused texture resource/descriptor was not reclaimed");
        for (int iteration = 0; iteration < 12; ++iteration) {
            const auto digest = std::string(63, 'f') + "0123456789ab"[iteration];
            auto cycle = textures->AcquireTextureRevision(Engine::Base::PathToUtf8(revisionTexture), digest);
            cycle.reset();
            Require(textures->CollectUnusedTextures() == 1 && textures->GetCachedTextureCount() == textureBaseline &&
                srvManager->GetUsedCount() == descriptorBaseline, "Repeated texture revisions accumulated active resources/SRVs");
        }
        auto getterPinned = textures->AcquireTextureRevision(Engine::Base::PathToUtf8(revisionTexture), std::string(64, '9'));
        const auto pinnedTextureKey = getterPinned->key;
        const auto pinnedTextureIndex = textures->GetTextureIndexByFilePath(pinnedTextureKey);
        getterPinned.reset();
        Require(textures->CollectUnusedTextures() == 0 && srvManager->IsAllocated(pinnedTextureIndex), "Legacy descriptor getter did not pin texture");
        auto loadPinned = textures->AcquireTextureRevision(Engine::Base::PathToUtf8(revisionTexture), std::string(64, '8'));
        const auto loadPinnedIndex = loadPinned->srvIndex;
        textures->LoadTexture(loadPinned->key); loadPinned.reset();
        Require(textures->CollectUnusedTextures() == 0 && srvManager->IsAllocated(loadPinnedIndex), "Legacy load did not pin texture");
        // Model ownership must release its otherwise unexposed texture lease too.
        const auto modelTextureBaseline = textures->GetCachedTextureCount();
        auto textureOwningModel = models->AcquireModel("model.obj", nativeRoot, "texture-owner-guid", std::string(64, '7'));
        const auto modelTextureIndex = textureOwningModel->GetModelData().materials.at(textureOwningModel->GetModelData().submeshes.front().materialIndex).textureIndex;
        Require(srvManager->IsAllocated(modelTextureIndex), "Model texture was not allocated");
        textureOwningModel.reset();
        Require(models->CollectUnusedModels() == 1 && !srvManager->IsAllocated(modelTextureIndex) &&
            textures->GetCachedTextureCount() == modelTextureBaseline, "Model collection did not release associated texture leases");
        std::cout << "PASS: actual GPU texture leases, shared owners, repeated resource/SRV collection, legacy pinning and model-owned texture release\n";
        DirectX::ScratchImage embeddedImage;
        Require(SUCCEEDED(embeddedImage.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 1)), "Embedded image fixture failed");
        std::memset(embeddedImage.GetPixels(), 255, embeddedImage.GetPixelsSize());
        DirectX::Blob embeddedPng;
        Require(SUCCEEDED(DirectX::SaveToWICMemory(*embeddedImage.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), embeddedPng)), "Embedded PNG encode failed");
        const auto embeddedBaseline = textures->GetCachedTextureCount();
        auto encoded = textures->AcquireTextureFromMemory("test://leased/encoded", embeddedPng.GetBufferPointer(), embeddedPng.GetBufferSize());
        auto rgba = textures->AcquireTextureFromRGBA("test://leased/rgba", 2, 2, embeddedImage.GetPixels(), embeddedImage.GetImage(0, 0, 0)->rowPitch);
        Require(encoded->metadata.width == 2 && rgba->metadata.height == 2 && textures->CollectUnusedTextures() == 0, "Embedded leases were not retained/decoded");
        const auto encodedIndex = encoded->srvIndex, rgbaIndex = rgba->srvIndex;
        auto sharedEncoded = textures->AcquireTextureFromMemory("test://leased/encoded", nullptr, 0);
        Require(sharedEncoded == encoded, "Embedded opaque key did not reuse its lease");
        encoded.reset(); rgba.reset();
        Require(textures->CollectUnusedTextures() == 1 && !srvManager->IsAllocated(rgbaIndex) && srvManager->IsAllocated(encodedIndex), "Embedded shared owner lifetime failed");
        sharedEncoded.reset();
        Require(textures->CollectUnusedTextures() == 1 && !srvManager->IsAllocated(encodedIndex) && textures->GetCachedTextureCount() == embeddedBaseline, "Embedded encoded texture was not reclaimed");
        bool invalidEmbedded = false;
        try { textures->AcquireTextureFromMemory("test://leased/invalid", "invalid image", 13); }
        catch (const std::exception&) { invalidEmbedded = true; }
        Require(invalidEmbedded && textures->GetCachedTextureCount() == embeddedBaseline, "Failed embedded decode polluted the cache");
        auto pinnedEmbedded = textures->AcquireTextureFromRGBA("test://leased/pinned", 2, 2, embeddedImage.GetPixels(), embeddedImage.GetImage(0, 0, 0)->rowPitch);
        const auto pinnedEmbeddedIndex = pinnedEmbedded->srvIndex;
        textures->LoadTextureFromRGBA("test://leased/pinned", 0, 0, nullptr, 0);
        pinnedEmbedded.reset();
        Require(textures->CollectUnusedTextures() == 0 && srvManager->IsAllocated(pinnedEmbeddedIndex), "Legacy embedded load failed to pin its cached entry");
        const auto base64 = [](const void* data, size_t size) {
            constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const auto* bytes = static_cast<const unsigned char*>(data); std::string output;
            for (size_t index = 0; index < size; index += 3) {
                const unsigned bits = (unsigned(bytes[index]) << 16) | (index + 1 < size ? unsigned(bytes[index + 1]) << 8 : 0) | (index + 2 < size ? unsigned(bytes[index + 2]) : 0);
                output += alphabet[(bits >> 18) & 63]; output += alphabet[(bits >> 12) & 63];
                output += index + 1 < size ? alphabet[(bits >> 6) & 63] : '=';
                output += index + 2 < size ? alphabet[bits & 63] : '=';
            }
            return output;
        };
        const float embeddedPositions[] = {0,0,0, 1,0,0, 0,1,0};
        { std::ofstream buffer(revisionRoot / "embedded.bin", std::ios::binary); buffer.write(reinterpret_cast<const char*>(embeddedPositions), sizeof(embeddedPositions)); Require(bool(buffer), "Embedded glTF buffer write failed"); }
        {
            std::ofstream gltf(revisionRoot / "embedded.gltf");
            gltf << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"embedded.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"images":[{"uri":"data:image/png;base64,)"
                << base64(embeddedPng.GetBufferPointer(), embeddedPng.GetBufferSize())
                << R"("}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
            Require(bool(gltf), "Embedded glTF write failed");
        }
        const auto embeddedModelBaseline = textures->GetCachedTextureCount();
        auto embeddedModel = models->AcquireModel("embedded.gltf", nativeRoot, "embedded-owner-guid", std::string(64, '5'));
        const auto& embeddedMaterial = embeddedModel->GetModelData().materials.at(embeddedModel->GetModelData().submeshes.front().materialIndex);
        const auto embeddedModelIndex = embeddedMaterial.textureIndex;
        Require(embeddedMaterial.textureFilePath.find("embedded_texture_") != std::string::npos && srvManager->IsAllocated(embeddedModelIndex), "Model did not import a real embedded texture");
        auto externalEmbeddedOwner = textures->AcquireTextureRevision(embeddedMaterial.textureFilePath, std::string(64, '5'));
        Require(externalEmbeddedOwner->metadata.width == 2, "Model embedded image metadata mismatch");
        embeddedModel.reset();
        Require(models->CollectUnusedModels() == 1 && srvManager->IsAllocated(embeddedModelIndex), "Model removal ignored external embedded owner");
        externalEmbeddedOwner.reset();
        Require(textures->CollectUnusedTextures() == 1 && !srvManager->IsAllocated(embeddedModelIndex) && textures->GetCachedTextureCount() == embeddedModelBaseline,
            "Model embedded texture lease was pinned or not reclaimed");
        std::cout << "PASS: actual GPU compressed/RGBA leases, invalid decode isolation, legacy pins and Assimp embedded glTF/model/external-owner lifetime\n";
        Engine::Assets::AssetRegistry liveRegistry; std::string revisionError;
        Require(liveRegistry.Scan(revisionRoot, revisionError), revisionError.c_str());
        const auto liveGuid = liveRegistry.FindByPath(revisionRoot / "model.obj")->guid;
        Engine::Scene::EntityWorld liveWorld;
        const auto liveId = liveWorld.CreateEntity("Revision model");
        liveWorld.SetModel(liveId, Engine::Base::PathToUtf8(revisionRoot / "model.obj"), liveGuid);
        Require(liveWorld.SetCamera(liveId, {true, 0.8f, 0.2f, 500, true, 12}), "GPU authored orthographic camera setup failed");
        Engine::Scene::EntityScene liveScene(liveWorld, *dxCommon, liveRegistry, GetProjectRoot());
        liveScene.Initialize();
        Require(liveRegistry.SetImportFingerprint(liveGuid, revisionA), "Live revision assignment failed");
        liveScene.SynchronizeModels(0);
        Require(liveScene.GetModelRenderStatus(liveId).loaded && liveScene.GetModelRenderStatus(liveId).revision == revisionA, "EntityScene did not load published revision");
        auto* liveCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
        Require(liveCamera && liveCamera->IsOrthographic() && std::abs(liveCamera->GetProjectionMatrix().m[1][1] - 1.0f / 6.0f) < 0.0001f &&
            liveCamera->GetProjectionMatrix().m[2][3] == 0, "EntityScene did not apply orthographic camera projection");
        Require(liveWorld.SetCamera(liveId, {true, 0.8f, 0.2f, 500}), "GPU camera perspective switch failed");
        liveScene.SynchronizeModels(0);
        Require(!liveCamera->IsOrthographic() && liveCamera->GetProjectionMatrix().m[2][3] == 1, "EntityScene did not restore perspective camera projection");
        std::cout << "PASS: actual EntityScene authored orthographic projection and perspective switching\n";
        Engine::CameraSystem::Camera editorCamera;
        editorCamera.SetTranslate({10,20,-30}); editorCamera.SetOrthographic(true, 20);
        const auto authoredBeforeEditorView = liveWorld.Capture().dump();
        liveScene.SetEditorCamera(&editorCamera); liveScene.SynchronizeModels(0);
        Require(Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera() == &editorCamera &&
            std::abs(editorCamera.GetProjectionMatrix().m[1][1] - 0.1f) < 0.0001f && editorCamera.GetWorldPosition().y == 20 &&
            liveWorld.Capture().dump() == authoredBeforeEditorView, "Editor camera override changed authored world or did not apply");
        liveScene.SetEditorCamera(nullptr); liveScene.SynchronizeModels(0);
        Require(Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera() == liveCamera &&
            Engine::CameraSystem::CameraManager::GetInstance()->GetCamera("__entity_scene_editor_camera") == nullptr &&
            liveWorld.Capture().dump() == authoredBeforeEditorView, "Editor camera detach did not restore authored camera/remove registration");
        std::cout << "PASS: actual EntityScene transient editor-camera override, authored-world isolation and detach\n";
        Engine::Editor::SceneBounds pickedBounds;
        Require(liveScene.GetWorldModelBounds(liveId,pickedBounds) && pickedBounds.max.x>pickedBounds.min.x, "Loaded model has no editor bounds");
        const float pickCenterX=(pickedBounds.min.x+pickedBounds.max.x)*0.5f, pickCenterY=(pickedBounds.min.y+pickedBounds.max.y)*0.5f;
        const Engine::Editor::ScenePickSegment modelRay{{pickCenterX,pickCenterY,-10},{pickCenterX,pickCenterY,10}};
        const auto fartherId=liveWorld.CreateEntity("Picking farther model");
        liveWorld.SetModel(fartherId,Engine::Base::PathToUtf8(revisionRoot / "model.obj"),liveGuid);
        liveWorld.Find(fartherId)->transform.position.z=5;
        liveScene.SynchronizeModels(0);
        Require(liveScene.PickModel(modelRay)==liveId, "Model picking did not choose nearest bounds");
        liveWorld.Find(liveId)->active=false;
        Require(liveScene.PickModel(modelRay)==fartherId, "Inactive model remained pickable");
        liveWorld.Find(liveId)->active=true;
        liveWorld.Find(fartherId)->transform.position.z=0;
        Require(liveScene.PickModel(modelRay)==liveId, "Model picking tie was not deterministic by ID");
        Require(liveScene.PickModel({{10000,10000,-10},{10000,10000,10}})==0, "Empty-space ray selected a model");
        liveWorld.DestroyEntity(fartherId); liveScene.SynchronizeModels(0);
        std::cout << "PASS: actual loaded-model world bounds, nearest/tie picking, inactive exclusion and empty-space miss\n";
        const auto viewCameraId=liveWorld.CreateEntity("Independent game-view camera");
        Require(liveWorld.SetCamera(viewCameraId,{true,0.8f,0.1f,100}), "Game view camera setup failed");
        liveWorld.Find(viewCameraId)->transform.position={pickCenterX,pickCenterY,-5};
        liveWorld.Find(liveId)->camera.primary=false;
        editorCamera.SetTranslate({100,100,-5}); editorCamera.SetOrthographic(false);
        liveScene.SetEditorCamera(&editorCamera); liveScene.SynchronizeModels(0);
        const auto viewsWorldBefore=liveWorld.Capture().dump();
        offscreenRenderManager->InitializeEditorView();
        Require(offscreenRenderManager->GetViewRenderTarget(true)!=offscreenRenderManager->GetViewRenderTarget(false) &&
            offscreenRenderManager->GetEditorViewHandle().ptr!=offscreenRenderManager->GetImGuiSceneTextureHandle().ptr, "Scene/Game views share a color resource or SRV");
        offscreenRenderManager->Begin(); srvManager->PreDraw(); liveScene.DrawGameView(); offscreenRenderManager->End();
        offscreenRenderManager->BeginEditorView(); liveScene.Draw(); offscreenRenderManager->EndEditorView();
        Require(liveWorld.Capture().dump()==viewsWorldBefore && Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()==&editorCamera,
            "Two view draws changed authored state or failed to restore editor camera");
        const auto pixels=[&](bool editorView) {
            auto* color=offscreenRenderManager->GetViewRenderTarget(editorView); const auto desc=color->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows=0; UINT64 rowBytes=0,total=0;
            dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
            D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=total; buffer.Height=1;
            buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))), "View pixel readback allocation failed");
            D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=color; source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint=footprint;
            dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);
            dxCommon->GetCommandList()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); dxCommon->CommandKick();
            void* mapped=nullptr; D3D12_RANGE range{0,size_t(total)};
            Require(SUCCEEDED(readback->Map(0,&range,&mapped)), "View pixel map failed");
            std::vector<unsigned char> result; result.reserve(size_t(rowBytes)*rows);
            const auto* bytes=static_cast<const unsigned char*>(mapped)+footprint.Offset;
            for (UINT row=0; row<rows; ++row) result.insert(result.end(),bytes+size_t(row)*footprint.Footprint.RowPitch,bytes+size_t(row)*footprint.Footprint.RowPitch+size_t(rowBytes));
            D3D12_RANGE written{0,0}; readback->Unmap(0,&written); return result;
        };
        const auto gamePixels=pixels(false), scenePixels=pixels(true);
        Require(gamePixels.size()==scenePixels.size() && !gamePixels.empty(), "View readback dimensions differ");
        size_t changedPixels=0;
        for (size_t index=0; index<gamePixels.size(); index+=4) if (std::memcmp(gamePixels.data()+index,scenePixels.data()+index,4)!=0) ++changedPixels;
        Require(changedPixels>100, "Independent cameras did not produce different rendered pixels");
        const auto viewSrvCount=srvManager->GetUsedCount();
        auto* gameColor=offscreenRenderManager->GetViewRenderTarget(false);
        const auto sceneHandle=offscreenRenderManager->GetEditorViewHandle().ptr;
        for (const auto extent : {Engine::Base::ViewExtent{320,640},Engine::Base::ViewExtent{900,300},Engine::Base::ViewExtent{1,1},Engine::Base::ViewExtent{640,480}}) {
            // Resize with an unsubmitted draw referencing the old color/depth.
            offscreenRenderManager->BeginEditorView(); srvManager->PreDraw(); liveScene.Draw(); offscreenRenderManager->EndEditorView();
            offscreenRenderManager->ResizeEditorView(extent);
            liveScene.SetSceneAspectRatio(extent.Aspect()); liveScene.SynchronizeModels(0);
            const auto projection=editorCamera.GetProjectionMatrix();
            Require(std::abs(projection.m[1][1]/projection.m[0][0]-extent.Aspect())<0.0001f,"Scene camera did not adopt resized aspect");
            offscreenRenderManager->Begin(); srvManager->PreDraw(); liveScene.DrawGameView(); offscreenRenderManager->End();
            const auto gameProjection=Engine::CameraSystem::CameraManager::GetInstance()->GetCamera("__entity_scene_camera")->GetProjectionMatrix();
            Require(std::abs(gameProjection.m[1][1]/gameProjection.m[0][0]-16.0f/9.0f)<0.0001f,"Scene resize changed authored Game view aspect");
            Require(std::abs(editorCamera.GetProjectionMatrix().m[1][1]/editorCamera.GetProjectionMatrix().m[0][0]-extent.Aspect())<0.0001f,"Game draw did not restore Scene aspect");
            offscreenRenderManager->BeginEditorView(); liveScene.Draw(); offscreenRenderManager->EndEditorView();
            const auto image=pixels(true);
            Require(image.size()==size_t(extent.width)*extent.height*4,"Resized Scene readback size failed");
            Require(offscreenRenderManager->GetViewRenderTarget(false)==gameColor && srvManager->GetUsedCount()==viewSrvCount && offscreenRenderManager->GetEditorViewHandle().ptr==sceneHandle,"Scene resize changed Game target or leaked descriptors");
            auto* current=offscreenRenderManager->GetViewRenderTarget(true);
            offscreenRenderManager->ResizeEditorView(extent);
            Require(offscreenRenderManager->GetViewRenderTarget(true)==current,"Same-sized Scene target was recreated");
            try { offscreenRenderManager->ResizeEditorView({0,64}); Require(false,"Invalid resize accepted"); }
            catch (const std::invalid_argument&) {}
            Require(offscreenRenderManager->GetViewRenderTarget(true)==current,"Invalid resize changed live target");
        }
        Require(liveWorld.Capture().dump()==viewsWorldBefore,"Scene resize mutated authored world");
        imGuiManager->Begin();
        bool resizeWindowOpen=true, sawAvailable=false, sawGizmo=false;
        Engine::CameraSystem::Camera gizmoCamera; gizmoCamera.SetTranslate({0,0,-5}); gizmoCamera.SetAspectRatio(0.5f); gizmoCamera.Update();
        const auto viewport=Engine::Editor::DebugEditorManager::DrawSceneViewport(&resizeWindowOpen,{},"Scene",0,
            [&](Vector2 available) {
                sawAvailable=available.x>0 && available.y>0;
                // The extent callback must run before image aspect is read.
                offscreenRenderManager->ResizeEditorView({300,600});
            }, [&](const Engine::Editor::DebugSceneViewportState& state) {
                using Gizmo=Engine::Editor::ImGuizmoManager;
                Matrix4x4 transform{}; for (int i=0;i<4;++i) transform.m[i][i]=1;
                const auto before=transform;
                Gizmo::SetDrawlist(); Gizmo::SetRect(state.min.x,state.min.y,state.size.x,state.size.y);
                Gizmo::SetOrthographic(false); Gizmo::Enable(false);
                const int vertices=ImGui::GetWindowDrawList()->VtxBuffer.Size;
                Gizmo::Manipulate(&gizmoCamera.GetViewMatrix().m[0][0],&gizmoCamera.GetProjectionMatrix().m[0][0],&transform.m[0][0],Gizmo::Operation::Translate,Gizmo::Mode::Local);
                sawGizmo=ImGui::GetWindowDrawList()->VtxBuffer.Size>vertices;
                Require(std::memcmp(&before,&transform,sizeof(transform))==0,"Disabled gizmo changed a transform");
                Engine::Editor::GizmoSnapSettings snapping; snapping.enabled=true;
                std::array<float,3> snap; std::string snapError;
                for (int operation=0;operation<3;++operation) {
                    Require(snapping.Resolve(operation,snap,snapError),"Snap resolution failed");
                    Gizmo::Manipulate(&gizmoCamera.GetViewMatrix().m[0][0],&gizmoCamera.GetProjectionMatrix().m[0][0],&transform.m[0][0],
                        static_cast<Gizmo::Operation>(operation),Gizmo::Mode::Local,snap.data());
                    Require(std::memcmp(&before,&transform,sizeof(transform))==0,"Disabled snapped gizmo changed a transform");
                }
                const float invalidSnap[3]={1,-1,1};
                bool snapRejected=false;
                try { Gizmo::Manipulate(&gizmoCamera.GetViewMatrix().m[0][0],&gizmoCamera.GetProjectionMatrix().m[0][0],&transform.m[0][0],Gizmo::Operation::Translate,Gizmo::Mode::Local,invalidSnap); }
                catch (const std::invalid_argument&) { snapRejected=true; }
                Require(snapRejected && std::memcmp(&before,&transform,sizeof(transform))==0,"Invalid snap reached backend or changed transform");
                return true; // An overlay consumes image input before picking.
            });
        Require(sawAvailable && viewport.drawn && std::abs(viewport.size.x/viewport.size.y-0.5f)<0.0001f,
            "Scene image did not use extent applied by its current available-area callback");
        Require(sawGizmo && !viewport.inputActive,"Scene gizmo geometry/overlay input isolation failed");
        imGuiManager->End();
        std::cout << "PASS: Scene overlay callback, ImGuizmo geometry, all snap parameter mappings/invalid rejection, disabled-transform isolation and picking input consumption\n";
        offscreenRenderManager->ResizeEditorView({640,480});
        liveScene.SetEditorCamera(nullptr); liveScene.SynchronizeModels(0);
        offscreenRenderManager->Begin(); srvManager->PreDraw(); liveScene.DrawGameView(); offscreenRenderManager->End();
        const auto previewProjection=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->GetProjectionMatrix();
        Require(std::abs(previewProjection.m[1][1]/previewProjection.m[0][0]-offscreenRenderManager->GetEditorViewExtent().Aspect())<0.0001f,"Authored Scene preview aspect was not restored after Game draw");
        liveScene.SetEditorCamera(&editorCamera); liveScene.SynchronizeModels(0);
        std::cout << "PASS: Scene resize with pending GPU draws, portrait/wide/tiny readbacks, camera aspect restore, stable SRVs and invalid/no-op isolation\n";
        liveWorld.Find(liveId)->camera.primary=true; liveWorld.DestroyEntity(viewCameraId); liveScene.SetEditorCamera(nullptr); liveScene.SynchronizeModels(0);
        std::cout << "PASS: separate Scene/Game resources and SRVs, two-camera actual model rendering, pixel-readback difference and authored-state isolation\n";
        const auto beforeDuplicate = liveWorld.Capture();
        const auto cachedBeforeDuplicate = models->GetCachedModelCount();
        Engine::Scene::EntityId duplicatedModel = 0;
        std::string duplicateError;
        Require(liveWorld.DuplicateSubtree(liveId, liveWorld.Find(liveId)->parent, duplicatedModel, duplicateError), "Live model subtree duplication failed");
        liveScene.SynchronizeModels(0);
        Require(liveScene.GetModelRenderStatus(duplicatedModel).loaded &&
            liveScene.GetModelRenderStatus(duplicatedModel).revision == liveScene.GetModelRenderStatus(liveId).revision &&
            models->GetCachedModelCount() == cachedBeforeDuplicate, "Duplicate did not share the loaded model revision cache");
        offscreenRenderManager->Begin(); srvManager->PreDraw(); liveScene.DrawGameView(); offscreenRenderManager->End();
        dxCommon->CommandKick();
        Require(liveWorld.Restore(beforeDuplicate, duplicateError), "Live duplicate rollback failed");
        liveScene.SynchronizeModels(0);
        Require(!liveScene.GetModelRenderStatus(duplicatedModel).loaded && liveScene.GetModelRenderStatus(liveId).loaded &&
            models->GetCachedModelCount() == cachedBeforeDuplicate, "Removing duplicate damaged the original model cache lease");
        std::cout << "PASS: actual EntityScene subtree duplication shares GPU model revisions and preserves original resources after clone removal\n";
        Require(liveRegistry.SetImportFingerprint(liveGuid, revisionB), "Live replacement revision failed");
        liveScene.SynchronizeModels(0);
        Require(liveScene.GetModelRenderStatus(liveId).revision == revisionB, "EntityScene did not reconcile updated revision");
        { std::ofstream invalid(revisionRoot / "model.obj"); invalid << "not a valid model"; }
        const std::string revisionC(64, 'c'), revisionD(64, 'd');
        Require(liveRegistry.SetImportFingerprint(liveGuid, revisionC), "Failed-revision assignment failed");
        liveScene.SynchronizeModels(0);
        Require(liveScene.GetModelRenderStatus(liveId).loaded && liveScene.GetModelRenderStatus(liveId).revision == revisionB &&
            liveScene.GetModelRenderStatus(liveId).failedRevision == revisionC, "Bad model replaced last valid render object or aborted process");
        const auto errorsAfterAttempt = Engine::Base::Logger::GetEntries().size();
        liveScene.SynchronizeModels(0);
        Require(Engine::Base::Logger::GetEntries().size() == errorsAfterAttempt, "Failed revision retried/logged every frame");
        saveModel(false);
        Require(liveRegistry.SetImportFingerprint(liveGuid, revisionD), "Repaired revision assignment failed");
        liveScene.SynchronizeModels(0);
        Require(liveScene.GetModelRenderStatus(liveId).revision == revisionD && liveScene.GetModelRenderStatus(liveId).failedRevision.empty(), "New repaired revision did not retry successfully");
        fs::remove(Engine::Assets::AssetRegistry::MetadataPath(revisionRoot / "model.obj"));
        liveScene.SynchronizeModels(0);
        Require(!liveScene.GetModelRenderStatus(liveId).loaded, "Missing GUID sidecar retained last-valid renderer instead of failing closed");
        liveScene.SetEditorCamera(&editorCamera); liveScene.SynchronizeModels(0);
        dxCommon->WaitForAllFrames(); liveScene.Finalize();
        Require(Engine::CameraSystem::CameraManager::GetInstance()->GetCamera("__entity_scene_editor_camera") == nullptr &&
            Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera() != &editorCamera, "Scene finalization retained its non-owning editor camera");
        std::cout << "PASS: actual EntityScene GPU revision reconcile, failed-load retention/suppression, recovery and missing-GUID fail-closed behavior\n";
        std::cout << "PASS: actual GPU model/texture revision resources, Japanese paths, geometry/metadata changes and retained old cache entries\n";
        models->LoadModels({"axis.obj", "models/axis/axis.obj", "models/axis/../axis/axis.obj"});
        auto* axis = models->FindModel("axis.obj");
        Require(axis != nullptr, "Batch model must load");
        Require(models->FindModelFromResourceRoot("./Resources/", "models/axis/axis.obj") == axis,
            "Model aliases must reuse the instance");
        const auto afterModels = srvManager->GetUsedCount();
        models->LoadModel(fs::absolute("Resources/models/axis/axis.obj").generic_string());
        Require(models->FindModel(fs::absolute("Resources/models/axis/axis.obj").generic_string()) == axis,
            "Absolute model path must reuse the instance");
        Require(srvManager->GetUsedCount() == afterModels, "Cached models must not allocate descriptors");
        Require(models->GetOrLoadModel("axis.obj") == axis, "GetOrLoad must reuse cached models");
        auto* common = Engine::Graphics3D::Object3DCommon::GetInstance();
        auto object = Engine::Graphics3D::Object3D::Create(common, "axis.obj");
        Require(!object->CanDrawSkinning(), "Static factory object must remain static");
        auto animated = Engine::Graphics3D::Object3D::Create(common, "walk.gltf");
        Require(animated->CanDrawSkinning(), "Animated factory object must initialize skinning");
        object->SetTranslate({1.0f, 2.0f, 3.0f});
        object->Update();
        animated->Update();
        const auto animationBeforeViewRefresh=animated->GetAnimationTime();
        animated->RefreshViewTransform();
        Require(animated->GetAnimationTime()==animationBeforeViewRefresh, "View transform refresh advanced animation");
        for (int fps : {30, 60, 144}) {
            auto timed = Engine::Graphics3D::Object3D::Create(common, "walk.gltf");
            timed->SetAnimationLoop(false);
            timed->SetAnimationUpdateStride(3);
            for (int frame = 0; frame < fps; ++frame) { timed->Update(1.0f / fps); }
            const auto* clip = models->FindModel("walk.gltf")->FindAnimationClip("default");
            Require(clip != nullptr, "Test model must have an animation clip");
            Require(std::abs(timed->GetAnimationTime() - (std::min)(1.0f, clip->duration)) < 0.0001f,
                "Animation timing must be independent of frame rate, stride and palette cache hits");
            const auto pausedTime = timed->GetAnimationTime();
            timed->Update(0.0f);
            Require(timed->GetAnimationTime() == pausedTime, "Object animation must pause with zero dt");
        }
        Require(object->GetTransform().translate.y == 2.0f, "Factory object transform must remain editable");
        bool rejected = false;
        try { Engine::Graphics3D::Object3D::Create(nullptr, "axis.obj"); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Factory must reject null common services");
        rejected = false;
        try { models->GetOrLoadModel("__missing_factory_model__.obj"); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Factory must report a missing model");
        dxCommon->CommandKick();
    }
};

class EditorViewportFixture : public Engine::Scene::Game {
protected:
    using Access = Engine::Scene::GameApplicationChecksAccess;
    using Extent = Engine::Base::ViewExtent;
    static constexpr Extent gameExtent_{640, 360};
    static constexpr const char* sceneBytes_ = R"({"version":1,"entities":[{"id":1,"parent":0,"name":"Axis model","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"Resources/models/axis/axis.obj"},{"id":2,"parent":0,"name":"Game camera","active":true,"transform":{"position":[0,0,-5],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","camera":{"enabled":true,"primary":true,"fovY":0.8,"nearClip":0.1,"farClip":100}}]})";

public:
    ~EditorViewportFixture() override {
        if (!fixture_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(fixture_, error);
        }
    }

    void Initialize() override {
        const auto root = std::filesystem::temp_directory_path() /
            (L"kubo-editor-viewport-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(std::filesystem::create_directory(root), "Editor viewport fixture directory already exists");
        fixture_ = root;
        std::filesystem::create_directory(fixture_ / "Assets");
        const auto models = fixture_ / "Resources/models/axis";
        std::filesystem::create_directories(models);
        std::filesystem::copy("Resources/models/axis", models, std::filesystem::copy_options::recursive);
        {
            std::ofstream output(fixture_ / "entry.scene", std::ios::binary);
            output << sceneBytes_;
            output.close();
            Require(!output.fail(), "Editor viewport fixture Scene write failed");
        }
        {
            std::ofstream manifest(fixture_ / "entry.kubo", std::ios::binary);
            manifest << R"({"name":"Editor viewport check","startupScene":"entry.scene","assetDirectory":"Assets"})";
            manifest.close();
            Require(!manifest.fail(), "Editor viewport fixture manifest write failed");
        }
        SetProjectRoot(fixture_);
        SetProjectFile(fixture_ / "entry.kubo");
        SetProjectAssetRoot(fixture_ / "Assets");
        SetGameOutputExtent(gameExtent_);
        SetEditorMode(true);
        std::string error;
        Require(ConfigureStartupScene(fixture_ / "entry.scene", error), error);
        Game::Initialize();
        Access::HideAssetBrowser(*this);
        Require(!HasPendingPrefabRecovery(), "Editor viewport fixture is blocked by Prefab recovery");
        Require(SelectEditorEntity(1, false, false, error), error);
        baseline_ = Access::World(*this);
        Require(!IsSceneDirty(), "Editor viewport fixture Scene began dirty");
    }

protected:
    void VerifyAuthoredState() {
        Require(!IsSceneDirty() && Access::World(*this) == baseline_ && GetEditorSelection() == std::vector<Engine::Scene::EntityId>{1},
            "Editor viewport checks changed the authored Scene or selection");
        std::ifstream input(fixture_ / "entry.scene", std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Require(bytes == sceneBytes_, "Editor viewport checks changed saved Scene bytes");
    }

private:
    std::filesystem::path fixture_;
    std::string baseline_;
};

class EditorToolbarChecks final : public EditorViewportFixture {
    struct Control {
        ImVec2 center{};
        bool seen = false;
        bool disabled = false;
    };

public:
    void Initialize() override {
        EditorViewportFixture::Initialize();
        current_ = this;
        Engine::Editor::renderInspectorChecksObserver = [](const char* label, ImVec2 min, ImVec2 max, bool disabled) {
            current_->controls_[label] = {{(min.x + max.x) / 2, (min.y + max.y) / 2}, true, disabled};
        };
        ImGuiContextHook hook{};
        hook.Type = ImGuiContextHookType_NewFramePre;
        hook.UserData = this;
        hook.Callback = [](ImGuiContext*, ImGuiContextHook* current) {
            static_cast<EditorToolbarChecks*>(current->UserData)->SendInput();
        };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    }

    ~EditorToolbarChecks() override {
        Engine::Editor::renderInspectorChecksObserver = nullptr;
        current_ = nullptr;
    }

    void Update() override {
        if (frame_ == 4) {
            Create("Shortcut edit");
            ImGui::SetWindowFocus("Game");
        }
        if (frame_ == 9) {
            Require(IsSceneDirty(), "Ctrl+S was accepted from the Game view");
            ImGui::SetWindowFocus("Entity Hierarchy");
        }
        if (frame_ == 14) {
            Require(!IsSceneDirty() && SceneContains("Shortcut edit"), "Ctrl+S did not save from Hierarchy");
            Create("Toolbar edit");
            RECT rect{};
            const auto window = winApp->GetHwnd();
            Require(GetWindowRect(window, &rect) != FALSE, "Cannot read toolbar test window bounds");
            rect.right = rect.left + 880;
            rect.bottom = rect.top + 640;
            SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144,144), reinterpret_cast<LPARAM>(&rect));
        }
        if (frame_ == 18) {
            Require(controls_["* entry.scene"].seen && !controls_["Save Scene (Ctrl+S)"].disabled,
                "Narrow toolbar lost the dirty Scene name or Save button");
        }
        if (frame_ == 23) {
            Require(!IsSceneDirty() && SceneContains("Toolbar edit"), "Toolbar Save did not publish the Scene");
            Create("Menu edit");
            Engine::Physics::ColliderComponent guideCollider;
            guideCollider.shape=Engine::Physics::ColliderShape::Capsule;
            Require(Access::Entities(*this).SetCollider(1,guideCollider) && Access::Entities(*this).SetLight(1,{}),
                "Could not add Scene guide fixture components");
        }
        if (frame_ == 34) {
            Require(!IsSceneDirty() && SceneContains("Menu edit"), "File menu Save did not publish the Scene");
        }
        if (frame_ == 43) {
            Require(controls_["Save Scene (Ctrl+S)"].disabled && !controls_["Pause Scene"].disabled,
                "Play did not disable Save or enable Pause");
        }
        if (frame_ == 52) {
            Require(controls_["Resume Scene"].seen && !controls_["Resume Scene"].disabled && controls_["Pause Scene"].disabled,
                "Pause did not expose Resume");
        }
        if (frame_ == 61) {
            Require(!IsSceneDirty() && !controls_["Save Scene (Ctrl+S)"].disabled && controls_["Stop and Restore Scene"].disabled,
                "Stop did not restore the editing controls and Scene state");
            std::cout << "PASS: Game focus guard; Ctrl+S, toolbar and File menu saves; dirty name and Play/Pause/Stop at 880px/144 DPI\n";
            std::string error;
            Require(SelectEditorEntity(1,false,false,error),error);
            ImGui::SetWindowFocus("Scene");
            Access::FocusSceneSelection(*this);
            sourceBeforeGuides_ = Access::World(*this);
            Access::SetSceneGuides(*this,true);
        }
        if (frame_ == 65) {
            const auto* sceneWindow = ImGui::FindWindowByName("Scene");
            Require(HasGuideColor(sceneWindow,IM_COL32(110,190,255,255)) &&
                HasGuideColor(sceneWindow,IM_COL32(115,234,162,255)) &&
                HasGuideColor(sceneWindow,IM_COL32(255,217,112,255)),
                "Scene draw list did not contain selection, collider and light guides");
            ImGui::SetWindowFocus("Game");
        }
        if (frame_ == 69) {
            const auto* gameWindow = ImGui::FindWindowByName("Game");
            Require(gameWindow && gameWindow->Active && !HasGuideColor(gameWindow,IM_COL32(110,190,255,255)) &&
                !HasGuideColor(gameWindow,IM_COL32(115,234,162,255)) && !HasGuideColor(gameWindow,IM_COL32(255,217,112,255)) &&
                !IsSceneDirty() && Access::World(*this) == sourceBeforeGuides_,
                "Scene guides affected the Game draw list or authored Scene");
            std::cout << "PASS: Scene selection, collider and light guides draw only in Scene and leave the authored world unchanged\n";
            endRequest_ = true;
        }
        Game::Update();
    }

    void Draw() override {
        Game::Draw();
        ++frame_;
    }

private:
    static bool HasGuideColor(const ImGuiWindow* window, ImU32 color) {
        return window && std::any_of(window->DrawList->VtxBuffer.begin(),window->DrawList->VtxBuffer.end(),
            [color](const ImDrawVert& vertex) {return vertex.col == color;});
    }

    void Create(const char* name) {
        Engine::Scene::EntityId created{};
        std::string error;
        Require(CreateEditorEntity(name, 0, created, error), error);
    }

    bool SceneContains(std::string_view name) const {
        std::ifstream input(GetProjectRoot() / "entry.scene", std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return bytes.find(name) != std::string::npos;
    }

    void SendInput() {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (frame_ >= 5 && frame_ <= 6) || (frame_ >= 10 && frame_ <= 11));
        io.AddKeyEvent(ImGuiKey_S, frame_ == 6 || frame_ == 11);
        const char* target = nullptr;
        int press = -1;
        if (frame_ >= 18 && frame_ <= 22) { target = "Save Scene (Ctrl+S)"; press = 20; }
        if (frame_ >= 24 && frame_ <= 28) { target = "File menu"; press = 26; }
        if (frame_ >= 29 && frame_ <= 33) { target = "Save Scene"; press = 31; }
        if (frame_ >= 36 && frame_ <= 42) { target = "Play Scene"; press = 39; }
        if (frame_ >= 45 && frame_ <= 51) { target = "Pause Scene"; press = 48; }
        if (frame_ >= 54 && frame_ <= 60) { target = "Stop and Restore Scene"; press = 57; }
        if (target) {
            const auto& control = controls_[target];
            Require(control.seen, target);
            io.AddMousePosEvent(control.center.x, control.center.y);
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, frame_ == press);
    }

    static inline EditorToolbarChecks* current_ = nullptr;
    std::string sourceBeforeGuides_;
    std::unordered_map<std::string, Control> controls_;
    int frame_ = 0;
};

class SceneAssetPlacementChecks final : public EditorViewportFixture {
public:
    void Initialize() override {
        EditorViewportFixture::Initialize();
        const auto model=GetProjectRoot()/L"Assets/配置モデル.obj";
        const auto prefab=GetProjectRoot()/L"Assets/配置プレハブ.prefab";
        std::filesystem::copy_file("Resources/models/axis/axis.obj",model);
        std::filesystem::copy_file("Resources/models/axis/axis.mtl",GetProjectRoot()/"Assets/axis.mtl");
        std::string error;
        std::filesystem::path saved;
        Require(SaveEditorPrefab(prefab,saved,error),error);
        auto& registry=Access::ControllerAssets(*this);
        Require(registry.Scan(GetProjectRoot()/"Assets",error),error);
        Require(registry.FindByPath(model) && registry.FindByPath(prefab),"Placement assets were not registered");
        modelGuid_=registry.FindByPath(model)->guid;
        prefabGuid_=registry.FindByPath(prefab)->guid;
        Access::ShowAssetBrowser(*this);
        Access::SetSceneOverlays(*this,false,false);
        current_=this;
        Engine::Editor::assetBrowserChecksObserver=[](const char* guid,uint64_t,Vector2 min,Vector2 max,bool) {
            const ImVec2 center{(min.x+max.x)*0.5f,(min.y+max.y)*0.5f};
            if (current_->modelGuid_==guid) current_->modelRow_=center;
            if (current_->prefabGuid_==guid) current_->prefabRow_=center;
            if (current_->imageGuid_==guid) current_->imageRow_=center;
        };
        Engine::Editor::renderInspectorChecksObserver=[](const char* name,ImVec2 min,ImVec2 max,bool disabled) {
            if (!disabled && std::string_view(name)=="Inspector asset drop")
                current_->dropPoint_={(min.x+max.x)*0.5f,(min.y+max.y)*0.5f};
        };
        ImGuiContextHook hook{};
        hook.Type=ImGuiContextHookType_NewFramePre;
        hook.UserData=this;
        hook.Callback=[](ImGuiContext*,ImGuiContextHook* current) {
            static_cast<SceneAssetPlacementChecks*>(current->UserData)->SendInput();
        };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    }

    ~SceneAssetPlacementChecks() override {
        Engine::Editor::assetBrowserChecksObserver=nullptr;
        Engine::Editor::renderInspectorChecksObserver=nullptr;
        current_=nullptr;
    }

    void Update() override {
        std::string error;
        if (frame_ == 3) {
            Arrange("Asset Browser",{20,65},{390,560});
            Arrange("Scene",{420,65},{650,560});
            ImGui::SetWindowFocus("Asset Browser");
            ImGui::SetWindowFocus("Scene");
            Access::FocusSceneSelection(*this);
            original_=Access::World(*this);
        }
        if (frame_ == 21) {
            auto& world=Access::Entities(*this);
            const auto ids=GetEditorSelection();
            const auto* placed=ids.size()==1 ? world.Find(ids.front()) : nullptr;
            Require(placed && placed->model.assetGuid==modelGuid_ && placed->name=="配置モデル" && IsSceneDirty(),
                "Dragging the registered model did not publish a selected Entity: "+Access::EditorStatus(*this));
            Require(std::abs(placed->transform.position.x)>0.01f &&
                (std::abs(placed->transform.position.y)<0.001f || std::abs(placed->transform.position.z)<0.001f),
                "Model was not placed on a Scene plane at the cursor");
            modelWorld_=world.Capture().dump();
            Require(ApplyEditorHistory(false,error) && Access::World(*this)==original_ && !IsSceneDirty(),
                "Model drag was not one Undo operation: "+error);
            Require(ApplyEditorHistory(true,error) && Access::World(*this)==modelWorld_ && IsSceneDirty(),
                "Model drag Redo lost the model binding: "+error);
            Access::SetFrontOrthographicView(*this);
        }
        if (frame_ == 39) {
            const auto ids=GetEditorSelection();
            const auto* linked=ids.size()==1 ? Access::Entities(*this).Find(ids.front()) : nullptr;
            Require(linked && !linked->prefabLink.is_null() && linked->prefabLink["sourceGuid"].get<std::string>()==prefabGuid_ &&
                std::abs(linked->transform.position.x)>0.01f && std::abs(linked->transform.position.y)>0.01f &&
                std::abs(linked->transform.position.z)<0.001f,
                "Prefab drag lost the source link or cursor position on the XY plane: "+Access::EditorStatus(*this));
            Require(ApplyEditorHistory(false,error) && Access::World(*this)==modelWorld_,
                "Prefab placement did not Undo in one operation");
            std::cout<<"PASS: Asset Browser drag for model and Prefab, Japanese name/GUID, perspective/XY placement, link and single-step Undo/Redo\n";
            const auto source=GetProjectRoot()/L"割り当て.png";
            std::filesystem::copy_file("Resources/textures/common/white.png",source);
            Require(Access::StartFileImport(*this,source),"Could not start browser texture import");
            Arrange("Entity Inspector",{420,65},{650,560});
            ImGui::SetWindowFocus("Entity Inspector");
        }
        if (frame_ > 39 && !spriteStart_) {
            const auto* image=Access::ControllerAssets(*this).FindByPath(GetProjectRoot()/L"Assets/割り当て.png");
            if (image && !Access::ImportBusy(*this)) {
                imageGuid_=image->guid;
                spriteStart_=frame_+6;
            } else Require(frame_<180,"Browser did not publish imported texture");
        }
        if (spriteStart_ && frame_==spriteStart_+13) {
            const auto ids=GetEditorSelection();
            const auto* assigned=ids.size()==1 ? Access::Entities(*this).Find(ids.front()) : nullptr;
            Require(assigned && assigned->hasSprite && assigned->sprite.assetGuid==imageGuid_ &&
                assigned->model.assetGuid==modelGuid_, "Inspector drop did not assign the imported Sprite image");
            Require(ApplyEditorHistory(false,error) && Access::World(*this)==modelWorld_,
                "Inspector drop did not Undo in one operation");
            std::cout<<"PASS: Browser published imported Japanese texture and Inspector drop assigned it with single-step Undo\n";
            endRequest_=true;
        }
        Game::Update();
    }

    void Draw() override {Game::Draw();++frame_;}

private:
    static void Arrange(const char* title,ImVec2 position,ImVec2 size) {
        auto* window=ImGui::FindWindowByName(title);
        Require(window!=nullptr,"Could not arrange asset fixture windows");
        ImGui::DockContextProcessUndockWindow(ImGui::GetCurrentContext(),window,true);
        ImGui::SetWindowPos(window,position,ImGuiCond_Always);
        ImGui::SetWindowSize(window,size,ImGuiCond_Always);
    }

    void SendInput() {
        auto& io=ImGui::GetIO();
        const bool modelSource=frame_>=7 && frame_<=9;
        const bool prefabSource=frame_>=25 && frame_<=27;
        const int spriteFrame = spriteStart_ ? frame_-spriteStart_ : -1;
        const bool imageSource = spriteFrame>=0 && spriteFrame<=2;
        if (modelSource || prefabSource || imageSource) {
            const auto row=imageSource ? imageRow_ : modelSource ? modelRow_ : prefabRow_;
            Require(row.x>0 && row.y>0,"Asset Browser did not draw the drag source");
            io.AddMousePosEvent(row.x,row.y);
        }
        if ((frame_>=10 && frame_<=17) || (frame_>=28 && frame_<=35)) {
            const auto* scene=ImGui::FindWindowByName("Scene");
            Require(scene && scene->Active,"Scene window unavailable during drag");
            const auto point=scene->InnerRect.GetCenter();
            io.AddMousePosEvent(point.x+20,point.y+20);
        }
        if (spriteFrame>=3 && spriteFrame<=10) {
            Require(dropPoint_.x>0 && dropPoint_.y>0,"Inspector asset drop target was not drawn");
            io.AddMousePosEvent(dropPoint_.x,dropPoint_.y);
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,(frame_>=8 && frame_<=15) ||
            (frame_>=26 && frame_<=33) || (spriteFrame>=1 && spriteFrame<=8));
    }

    static inline SceneAssetPlacementChecks* current_{};
    ImVec2 modelRow_{},prefabRow_{},imageRow_{},dropPoint_{};
    std::string modelGuid_,prefabGuid_,imageGuid_,original_,modelWorld_;
    int frame_{},spriteStart_{};
};

class EditorInputFocusChecks final : public EditorViewportFixture {
public:
    explicit EditorInputFocusChecks(bool oversizedConsole = false)
        : oversizedConsole_(oversizedConsole) {}

    void Initialize() override {
        EditorViewportFixture::Initialize();
        std::ofstream probe(GetProjectRoot() / "Assets/input-probe.lua");
        probe << R"(function on_update(entity, dt)
    entity:set_rotation(input.available() and 1 or -1, input.mouse_wheel(), 0)
end
)";
        probe.close();
        Require(!probe.fail(), "Cannot write Lua input probe");
        Engine::Editor::gameInputAdmissionChecksObserver = [](bool allowed, float wheel) {
            admittedInput_ = allowed;
            admittedWheel_ = wheel;
        };
        Engine::Editor::renderInspectorChecksObserver = [](const char* label, ImVec2 min, ImVec2 max, bool) {
            const std::string_view name(label);
            const ImVec2 center{(min.x + max.x) / 2, (min.y + max.y) / 2};
            if (name == "Scene camera menu") cameraButton_ = center;
            if (name == "View menu") viewButton_ = center;
            if (name == "Windows menu") windowsButton_ = center;
            if (name == "Console") consoleButton_ = center;
            if (name == "Reset Dock Layout") resetButton_ = center;
        };
        ImGuiContextHook hook{};
        hook.Type = ImGuiContextHookType_NewFramePre;
        hook.UserData = this;
        hook.Callback = [](ImGuiContext*, ImGuiContextHook* current) {
            static_cast<EditorInputFocusChecks*>(current->UserData)->SendInput();
        };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    }

    ~EditorInputFocusChecks() override {
        Engine::Editor::renderInspectorChecksObserver = nullptr;
        Engine::Editor::gameInputAdmissionChecksObserver = nullptr;
    }

    void Update() override {
        if (frames_ == 8) {
            ImGui::SetWindowFocus("Game");
            std::string error;
            Require(PlayEditorScene(error), error);
            auto& world = Access::Entities(*this);
            inputProbe_ = world.CreateEntity("Runtime input probe");
            Require(world.SetScript(inputProbe_, "Assets/input-probe.lua"), "Cannot attach Lua input probe");
        }
        if (frames_ == 15) ImGui::SetWindowFocus("Scene");
        if (frames_ == 17) {
            std::string error;
            Require(Access::Stop(*this, error), error);
        }
        if (frames_ == 30) {
            initialDpi_ = winApp->GetWindowDpi();
            changedDpi_ = initialDpi_ == 144 ? 192 : 144;
            const auto window = winApp->GetHwnd();
            Require(GetWindowRect(window, &originalRect_) != FALSE, "Cannot read Editor bounds before DPI transition");
            RECT suggested{originalRect_.left, originalRect_.top, originalRect_.left + 880, originalRect_.top + 640};
            SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(changedDpi_, changedDpi_), reinterpret_cast<LPARAM>(&suggested));
            RECT observed{};
            Require(GetWindowRect(window, &observed) && EqualRect(&suggested, &observed),
                "Editor did not adopt the DPI transition bounds");
        }
        if (frames_ == 34) {
            ImGui::ClosePopupToLevel(0, true);
            auto* console = ImGui::FindWindowByName("Console");
            Require(console != nullptr, "Console panel was not instantiated");
            ImGui::DockContextProcessUndockWindow(ImGui::GetCurrentContext(), console, true);
            const auto* viewport = ImGui::GetMainViewport();
            if (oversizedConsole_) {
                ImGui::SetWindowPos(console, {viewport->WorkPos.x + 24, viewport->WorkPos.y + 48}, ImGuiCond_Always);
                ImGui::SetWindowSize(console, {viewport->WorkSize.x + 200, viewport->WorkSize.y + 200}, ImGuiCond_Always);
            } else {
                ImGui::SetWindowPos(console, {3500, 2200}, ImGuiCond_Always);
                ImGui::SetWindowSize(console, {640, 320}, ImGuiCond_Always);
            }
        }
        if (frames_ == 57) Access::ShowAssetBrowser(*this);
        if (frames_ == 68) {
            const auto window = winApp->GetHwnd();
            SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(initialDpi_, initialDpi_), reinterpret_cast<LPARAM>(&originalRect_));
        }
        admittedInput_.reset();
        Game::Update();
        if (frames_ == 6) {
            moved_ = Access::SceneCameraPosition(*this);
            Require(!SamePosition(framed_, moved_) && !Access::GameViewportReceivesInput(*this),
                "Scene RMB+W did not move the Editor camera exclusively");
        }
        if (frames_ == 13) {
            Require(ImGui::IsKeyDown(ImGuiKey_W) && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
                Access::GameViewportReceivesInput(*this) && SamePosition(moved_, Access::SceneCameraPosition(*this)),
                "Game viewport did not own input exclusively or moved the Editor camera");
            Require(admittedInput_ == true, "Playing Game did not admit input over the Game image");
            foregroundInput_ = GetForegroundWindow() == winApp->GetHwnd();
            VerifyLuaInput(foregroundInput_, 0.0f);
        }
        if (frames_ == 11 || frames_ == 12) {
            const float expectedWheel = frames_ == 11 ? 1.0f : 0.0f;
            Require(admittedInput_ == true && admittedWheel_ == expectedWheel,
                "Game did not admit the wheel delta exactly once");
            VerifyLuaInput(GetForegroundWindow() == winApp->GetHwnd(), expectedWheel);
        }
        if (frames_ == 15) {
            Require(admittedInput_ == false && !Access::GameViewportReceivesInput(*this),
                "Game admitted input on the frame that switched to Scene");
            Require(admittedWheel_ == 0.0f, "Game admitted a wheel event on the Scene transition");
            VerifyLuaInput(false, 0.0f);
        }
        if (frames_ == 20) {
            Require(!Access::GameViewportReceivesInput(*this) && SamePosition(framed_, Access::SceneCameraPosition(*this)),
                "F in the Scene viewport did not frame the selection after returning from Game");
        }
        if (frames_ == 22) {
            beforePopup_ = Access::SceneCameraPosition(*this);
            Require(!SamePosition(framed_, beforePopup_), "Scene mouse wheel did not zoom before the popup check");
        }
        if (frames_ == 28) {
            Require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                ImGui::IsKeyPressed(ImGuiKey_F, false) && SamePosition(beforePopup_, Access::SceneCameraPosition(*this)) &&
                !Access::GameViewportReceivesInput(*this), "Camera popup did not block viewport input");
        }
        if (frames_ == 35 || frames_ == 73) {
            const auto expectedDpi = frames_ == 35 ? changedDpi_ : initialDpi_;
            const auto expectedScale = static_cast<float>(expectedDpi) / USER_DEFAULT_SCREEN_DPI;
            Require(std::abs(ImGui::GetStyle().FontScaleDpi - expectedScale) < 0.001f,
                "Editor UI did not adopt the DPI transition scale");
            const auto* scene = ImGui::FindWindowByName("Scene");
            Require(scene && scene->Active && scene->DrawList->VtxBuffer.Size > 0,
                "Scene panel did not render after DPI transition");
        }
        if (frames_ == 35) {
            const auto* console = ImGui::FindWindowByName("Console");
            const auto* viewport = ImGui::GetMainViewport();
            Require(console && console->Active && console->DockId == 0 &&
                console->Pos.x + console->Size.x > viewport->WorkPos.x + viewport->WorkSize.x,
                "Console fixture was not already open beyond the workspace edge");
            if (oversizedConsole_) {
                Require(console->Pos.x >= viewport->WorkPos.x &&
                    console->Pos.y >= viewport->WorkPos.y &&
                    console->Pos.y + ImGui::GetFrameHeight() < viewport->WorkPos.y + viewport->WorkSize.y &&
                    console->SizeFull.x > viewport->WorkSize.x && console->SizeFull.y > viewport->WorkSize.y,
                    "Oversized Console must have a reachable title and exceed both workspace dimensions");
            }
        }
        if (frames_ == 54) {
            const auto* console = ImGui::FindWindowByName("Console");
            const auto* viewport = ImGui::GetMainViewport();
            Require(console && console->Active && console->DockId == 0 && console->DrawList->VtxBuffer.Size > 0 &&
                console->Pos.x >= viewport->WorkPos.x && console->Pos.y >= viewport->WorkPos.y &&
                console->Pos.x + console->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x &&
                console->Pos.y + console->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y,
                "View > Windows did not restore the floating Console inside the scaled workspace");
        }
        if (frames_ == 66 || frames_ == 73) {
            const auto* console = ImGui::FindWindowByName("Console");
            const auto* assets = ImGui::FindWindowByName("Asset Browser");
            const auto* scene = ImGui::FindWindowByName("Scene");
            const auto* game = ImGui::FindWindowByName("Game");
            Require(console && assets && scene && game && console->DockId != 0 &&
                console->DockId == assets->DockId && scene->DockId != 0 && scene->DockId == game->DockId &&
                scene->DockId != console->DockId,
                "View > Reset Dock Layout did not restore the viewport and bottom panel groups");
        }
        if (frames_ == 74) endRequest_ = true;
    }

    void Draw() override {
        Game::Draw();
        ++frames_;
        if (frames_ == 1) {
            ImGui::SetWindowFocus("Scene");
            Access::FocusSceneSelection(*this);
            framed_ = Access::SceneCameraPosition(*this);
        }
    }

    void Verify() {
        Require(frames_ == 74, "Editor viewport input and DPI sequence did not finish");
        VerifyAuthoredState();
        std::cout << "PASS: Scene/Game dock-tab input ownership, Scene RMB+W/F/wheel, popup input lock, Game input admission, unchanged authored Scene\n";
        std::cout << "PASS: Lua input.available() matched foreground ownership over Game and was false on the Scene transition frame; foreground_game="
            << foregroundInput_ << '\n';
        std::cout << "PASS: simulated WM_DPICHANGED " << initialDpi_ << " -> " << changedDpi_ << " -> " << initialDpi_
            << ", Win32 bounds, ImGui font scale, Scene rendering and one-click recovery of "
            << (oversizedConsole_ ? "an oversized Console with a reachable title" : "an off-screen floating Console") << '\n';
        std::cout << "PASS: View > Reset Dock Layout at high DPI restored Scene/Game and Console/Asset Browser dock groups through the return to original DPI\n";
    }

private:
    void VerifyLuaInput(bool available, float wheel) {
        const auto* probe = Access::Entities(*this).Find(inputProbe_);
        const auto fault = Access::Scripts(*this).GetLastError(inputProbe_);
        Require(fault.empty(), fault);
        Require(probe && probe->transform.rotation.x == (available ? 1.0f : -1.0f),
            "Lua did not receive the expected input.available() value");
        Require(probe->transform.rotation.y == (available ? wheel : 0.0f),
            "Lua did not receive the expected input.mouse_wheel() value");
    }

    static bool SamePosition(Vector3 a, Vector3 b) {
        return std::abs(a.x-b.x) < 0.001f && std::abs(a.y-b.y) < 0.001f && std::abs(a.z-b.z) < 0.001f;
    }

    void SendInput() {
        if (frames_ < 4) return;
        auto& io = ImGui::GetIO();
        const bool game = frames_ >= 10 && frames_ < 15;
        const auto* window = ImGui::FindWindowByName(game ? "Game" : "Scene");
        if (frames_ >= 23 && frames_ <= 25) {
            io.AddMousePosEvent(cameraButton_.x, cameraButton_.y);
        } else if (frames_ >= 36 && frames_ <= 52) {
            const auto button = frames_ < 40 ? viewButton_ : frames_ < 46 ? windowsButton_ : consoleButton_;
            io.AddMousePosEvent(button.x, button.y);
        } else if (frames_ >= 57 && frames_ <= 64) {
            const auto button = frames_ < 60 ? viewButton_ : resetButton_;
            io.AddMousePosEvent(button.x, button.y);
        } else if (window) {
            const auto center = window->InnerRect.GetCenter();
            // Stay within the image and clear of the selected object's gizmo.
            io.AddMousePosEvent(center.x + 140.0f, center.y + 50.0f);
        }
        // Queue the button down a frame before the key, matching a fly gesture
        // and retaining ImGui's default input-event trickling.
        io.AddKeyEvent(ImGuiKey_W, frames_ == 6 || frames_ == 13);
        io.AddKeyEvent(ImGuiKey_F, frames_ == 13 || frames_ == 20 || frames_ == 28);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, (frames_ >= 5 && frames_ <= 7) || (frames_ >= 12 && frames_ <= 13));
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,
            frames_ == 24 || frames_ == 37 || frames_ == 42 || frames_ == 49 || frames_ == 58 || frames_ == 62);
        if (frames_ == 11 || frames_ == 15 || frames_ == 22 || frames_ == 28) io.AddMouseWheelEvent(0, 1);
    }

    static inline ImVec2 cameraButton_{};
    static inline std::optional<bool> admittedInput_;
    static inline float admittedWheel_ = 0.0f;
    static inline ImVec2 viewButton_{}, windowsButton_{}, consoleButton_{}, resetButton_{};
    Vector3 framed_{}, moved_{}, beforePopup_{};
    RECT originalRect_{};
    UINT initialDpi_ = 0, changedDpi_ = 0;
    Engine::Scene::EntityId inputProbe_ = 0;
    bool foregroundInput_ = false;
    const bool oversizedConsole_;
    int frames_ = 0;
};

class EditorEnduranceChecks final : public EditorViewportFixture {
    using Clock = std::chrono::steady_clock;
    static constexpr int warmupFrames_ = 30;
    static constexpr int measuredFrames_ = 240;
    // Repeat in reverse order to expose thermal or clock drift during the run.
    static constexpr std::array overlayOrder_{0, 1, 2, 3, 3, 2, 1, 0};
    static constexpr int blockLength_ = warmupFrames_ + measuredFrames_;
    static constexpr int measurementEnd_ = static_cast<int>(overlayOrder_.size()) * blockLength_;
    static constexpr int resizeInterval_ = 80;
    static constexpr int totalFrames_ = measurementEnd_ + 6 * resizeInterval_;

public:
    void Update() override {
        if (frames_ < measurementEnd_) {
            const int phase = overlayOrder_[frames_ / blockLength_];
            const int offset = frames_ % blockLength_;
            if (offset == 0) Access::SetSceneOverlays(*this, (phase & 1) != 0, (phase & 2) != 0);
            if (offset == warmupFrames_) {
                dxCommon->ResetGpuTimingStatistics();
                started_ = Clock::now();
            }
        } else if ((frames_ - measurementEnd_) % resizeInterval_ == 0) {
            const int step = (frames_ - measurementEnd_) / resizeInterval_;
            const bool narrow = step % 2 == 0;
            Access::SetSceneOverlays(*this, (step & 1) != 0, (step & 2) != 0);
            Require(SetWindowPos(winApp->GetHwnd(), nullptr, 0, 0, narrow ? 960 : 1280, narrow ? 620 : 760,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Endurance window resize failed");
        }
        Game::Update();
        if (frames_ == totalFrames_) endRequest_ = true;
    }

    void Draw() override {
        Game::Draw();
        ++frames_;
        // The default dock shares Scene and Game in one tab bar. The test
        // measures Scene while that tab is selected, as during authoring.
        if (frames_ == 1) {
            ImGui::SetWindowFocus("Scene");
            Access::FocusSceneSelection(*this);
        }
        Require(offscreenRenderManager->HasEditorView() && offscreenRenderManager->GetViewRenderTarget(false),
            "Endurance lost a Scene or Game render target");
        Require(GetGameOutputExtent() == gameExtent_, "Endurance resize changed fixed Game output");
        const auto extent = offscreenRenderManager->GetEditorViewExtent();
        if (frames_ <= measurementEnd_) {
            if (frames_ == warmupFrames_) {
                measuredExtent_ = extent;
                priorResizeExtent_ = extent;
                auto* scene = dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
                Require(scene && scene->GetModelRenderStatus(1).loaded, "Endurance model did not load");
            }
            if (frames_ > warmupFrames_) Require(extent == measuredExtent_, "Scene extent changed during GPU comparison");
            if (frames_ % blockLength_ == 0) {
                const int block = frames_ / blockLength_ - 1;
                const int phase = overlayOrder_[block];
                const auto samples = dxCommon->GetFrameGpuSampleCount();
                const auto average = dxCommon->GetAverageFrameGpuMilliseconds();
                const auto p95 = dxCommon->GetFrameGpuP95Milliseconds();
                const auto max = dxCommon->GetFrameGpuMaxMilliseconds();
                const auto wall = std::chrono::duration<double, std::milli>(Clock::now() - started_).count() / measuredFrames_;
                const auto* sceneWindow = ImGui::FindWindowByName("Scene");
                Require(sceneWindow && sceneWindow->Active && sceneWindow->DrawList->VtxBuffer.Size > 0,
                    "Scene window was not drawn during GPU comparison");
                const auto vertices = sceneWindow->DrawList->VtxBuffer.Size;
                if (block == 0) baselineSceneVertices_ = vertices;
                Require(samples >= measuredFrames_ - 8 && std::isfinite(average) && average > 0 &&
                    std::isfinite(p95) && p95 > 0 && std::isfinite(max) && max >= p95, "GPU timing did not yield valid samples");
                std::cout << "EDITOR_TIMING pass=" << (block < 4 ? 1 : 2)
                    << " grid=" << (phase & 1) << " gizmo=" << ((phase & 2) != 0)
                    << " scene=" << extent.width << 'x' << extent.height << " scene_vertices=" << vertices << " samples=" << samples
                    << " gpu_mean_ms=" << average << " gpu_p95_ms=" << p95 << " gpu_max_ms=" << max
                    << " wall_mean_ms=" << wall << '\n';
                Require((phase & 2) == 0 ? vertices == baselineSceneVertices_ : vertices > baselineSceneVertices_,
                    "Scene draw geometry did not reflect the gizmo setting");
            }
        } else if ((frames_ - measurementEnd_) % resizeInterval_ == 0) {
            Require(extent != priorResizeExtent_, "Scene target did not follow the window resize");
            priorResizeExtent_ = extent;
            std::cout << "EDITOR_RESIZE frame=" << frames_ << " scene=" << extent.width << 'x' << extent.height << '\n';
        }
    }

    void Verify() {
        Require(frames_ == totalFrames_ && !offscreenRenderManager->HasEditorView(),
            "Endurance draw count or Scene target retirement differs");
        VerifyAuthoredState();
#ifdef _DEBUG
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue))), "DX12 debug queue is unavailable");
        for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index) {
            SIZE_T size = 0;
            if (FAILED(queue->GetMessage(index, nullptr, &size))) continue;
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(queue->GetMessage(index, message, &size))) {
                Require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                    message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
            }
        }
        std::cout << "PASS: DX12 debug queue contains no Error or Corruption messages\n";
#endif
        std::cout << "PASS: " << frames_ << " Editor frames, six resizes, fixed Game output and unchanged Scene\n";
    }

private:
    Extent measuredExtent_{};
    Extent priorResizeExtent_{};
    Clock::time_point started_{};
    int baselineSceneVertices_ = 0;
    int frames_ = 0;
};

class GenericViewsChecks final : public Engine::Scene::Game {
public:
    ~GenericViewsChecks() override {
        std::error_code error;
        if(!originalCwd_.empty()) std::filesystem::current_path(originalCwd_,error);
        if(!fixture_.empty()) std::filesystem::remove_all(fixture_,error);
    }
    void Initialize() override {
        originalCwd_=std::filesystem::current_path();
        fixture_=std::filesystem::temp_directory_path() / (L"kubo-generic-views-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(fixture_);
        const auto ownedModels=fixture_/"Resources/models/axis";
        std::filesystem::create_directories(ownedModels);
        std::filesystem::copy(originalCwd_/"Resources/models/axis",ownedModels,std::filesystem::copy_options::recursive);
        std::filesystem::create_directories(fixture_/"Unrelated");
        std::filesystem::create_directories(fixture_/"UnrelatedAgain");
        const auto shadowFont=fixture_/"Unrelated/EditorResources/Fonts/fa-solid-900.ttf";
        std::filesystem::create_directories(shadowFont.parent_path());
        {std::ofstream output(shadowFont);output<<"invalid cwd shadow font";Require(bool(output),"Shadow font fixture write failed");}
        for(const auto* relative : {"Assets/色.png","Assets/追加.png"}) {
            const auto texture=Engine::Base::PathFromUtf8(relative);
            std::filesystem::create_directories((fixture_/texture).parent_path());
            std::filesystem::copy_file(originalCwd_/"Resources/textures/common/uvChecker.png",fixture_/texture);
            for(const auto* unrelated : {"Unrelated","UnrelatedAgain"}) {
                const auto competitor=fixture_/unrelated/texture;
                std::filesystem::create_directories(competitor.parent_path());
                std::filesystem::copy_file(originalCwd_/"Resources/textures/common/white.png",competitor);
            }
        }
        const auto scene=fixture_/"views.scene";
        { std::ofstream output(scene); output << R"({"version":1,"entities":[{"id":1,"parent":0,"name":"Axis model","active":true,"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"model":"Resources/models/axis/axis.obj"},{"id":2,"parent":0,"name":"Game camera","active":true,"transform":{"position":[0,0,-5],"rotation":[0,0,0],"scale":[1,1,1]},"model":"","camera":{"enabled":true,"primary":true,"fovY":0.8,"nearClip":0.1,"farClip":100}}]})"; Require(bool(output),"Generic views fixture write failed"); }
        {std::ifstream input(scene,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};input.close();std::ofstream output(scene,std::ios::binary);output<<"\xef\xbb\xbf"<<bytes;Require(bool(output),"BOM Editor Scene fixture write failed");}
        const auto host=ImHashStr("WindowOverViewport_11111111");dockId_=ImHashStr("DockSpace",0,host);
        char docking[512];ImFormatString(docking,IM_ARRAYSIZE(docking),"[Docking][Data]\nDockSpace ID=0x%08X Window=0x%08X Pos=0,0 Size=1280,720 Split=X\n  DockNode ID=0x12345671 Parent=0x%08X SizeRef=321,720 CentralNode=1\n  DockNode ID=0x12345672 Parent=0x%08X SizeRef=959,720\n",dockId_,host,dockId_,dockId_);
        legacyLayout_=std::string("[Window][日本語MigrationMarker]\nPos=13,27\nSize=222,333\nCollapsed=1\n\n[Window][Scene]\nPos=0,0\nSize=321,720\nCollapsed=0\nDockId=0x12345671,0\n\n[Window][Game]\nPos=321,0\nSize=959,720\nCollapsed=0\nDockId=0x12345672,0\n\n")+docking;
        std::string error;
        originalScene_=scene;
        // Owned fixture starts off-grid; exercise the same command used by the toolbar.
        Engine::Scene::SceneValue gridFixture;
        Require(Engine::Scene::SceneDocument::Read(scene,gridFixture,error),error);
        gridFixture["entities"][size_t(0)]["transform"]["position"]=crude_json::array{0.25,-0.25,0.74};
        {std::ofstream output(scene,std::ios::binary);output<<"\xef\xbb\xbf"<<gridFixture.dump();output.close();Require(!output.fail(),"Grid Editor fixture write failed");}
        Engine::Scene::SceneValue alternate;
        Require(Engine::Scene::SceneDocument::Read(scene,alternate,error),error);
        alternate["entities"][size_t(0)]["id"]=3.0;
        alternate["entities"][size_t(0)]["name"]="Opened model";
        alternate["entities"][size_t(1)]["id"]=4.0;
        alternateScene_=fixture_/L"Assets/場面/別.scene";
        std::filesystem::create_directories(alternateScene_.parent_path());
        alternateBytes_="\xef\xbb\xbf"+alternate.dump();WriteAlternate(alternateBytes_);
        Require(Engine::Editor::EditorSettingsFile::Write(fixture_/"imgui.ini",legacyLayout_,error),error);
        Require(Engine::Editor::EditorSettingsFile::Write(fixture_/".debug_editor_windows.ini","MigrationMarker=0\n",error),error);
        SetProjectRoot(fixture_);
        SetGameOutputExtent({960,540});
        manifest_=fixture_/L"設定.kubo";
        manifestBytes_=R"({"name":"Settings UI","startupScene":"views.scene","gameOutput":{"width":512,"height":768},"extension":"preserve"})";
        {std::ofstream output(manifest_,std::ios::binary);output<<manifestBytes_;Require(bool(output),"Project settings UI manifest write failed");}
        SetProjectFile(manifest_);
        SetProjectAssetRoot(fixture_/"Assets");
        Engine::Scene::EntityWorld prefabSourceWorld;Require(prefabSourceWorld.Load(scene,error),error);
        Engine::Editor::PrefabValue prefabDefinition;Require(Engine::Editor::CapturePrefabSubtree(prefabSourceWorld,1,prefabDefinition,error),error);
        const auto prefabSource=fixture_/L"Assets/プレハブ/モデル.prefab";
        Require(Engine::Editor::SavePrefabDocument(prefabSource,prefabDefinition,error),error);
        Engine::Assets::AssetRegistry prefabRegistry;Require(prefabRegistry.Scan(fixture_/"Assets",error),error);
        prefabGuid_=prefabRegistry.FindByPath(prefabSource)->guid;
        // Real publication/archive plus a retained before-park history marker:
        // Game integration fixture, not a claim of an actual Editor crash.
        Engine::Base::ProjectContext recoveryProject;Require(recoveryProject.Open(manifest_,error),error);
        Engine::Editor::ProjectPrefabRevision recoveryRevision;
        Require(Engine::Editor::ReadProjectPrefabRevision(recoveryProject,prefabRegistry,prefabGuid_,recoveryRevision,error),error);
        auto recoveryNext=prefabDefinition;recoveryNext["world"]["entities"][size_t(0)]["name"]="History marker next source";
        {Engine::Editor::PrefabSourcePublication publication;Require(publication.Prepare(recoveryProject,prefabRegistry,recoveryRevision,recoveryNext,error) && publication.Publish(error),error);}
        Engine::Editor::PrefabSourceRecoveryReport recoveryReceipt;std::filesystem::path recoveryArchive;
        Require(Engine::Editor::PrefabSourceRecovery::Inspect(recoveryProject,prefabGuid_,recoveryReceipt,error) && Engine::Editor::PrefabSourceAcknowledgement::ArchiveResolved(recoveryProject,prefabGuid_,recoveryReceipt,recoveryArchive,error),error);
        const auto pendingHistory=fixture_/"Assets/.kubo/prefab-transactions"/prefabGuid_;
        std::filesystem::create_directory(pendingHistory);
        const auto fixtureRead=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Require(bool(in),"Recovery integration fixture read failed");return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());};
        Engine::Editor::PrefabValue historyMarker;historyMarker["format"]="kubo-prefab-source-history-1";historyMarker["guid"]=prefabGuid_;
        historyMarker["archiveRelative"]=Engine::Base::PathToUtf8(recoveryArchive.lexically_relative(fixture_/"Assets"));historyMarker["undo"]=true;
        historyMarker["intent"]=Engine::Editor::PrefabValue::parse(fixtureRead(recoveryArchive/"pending.json"));
        {std::ofstream out(pendingHistory/"history.json",std::ios::binary);out<<historyMarker.dump();out.close();Require(!out.fail(),"History integration marker write failed");}
        const auto pendingPublicationSource=fixture_/L"Assets/プレハブ/未確定.prefab";
        Require(Engine::Editor::SavePrefabDocument(pendingPublicationSource,prefabDefinition,error) && prefabRegistry.Scan(fixture_/"Assets",error),error);
        const auto pendingPublicationGuid=prefabRegistry.FindByPath(pendingPublicationSource)->guid;
        Engine::Editor::ProjectPrefabRevision publicationRevision;Require(Engine::Editor::ReadProjectPrefabRevision(recoveryProject,prefabRegistry,pendingPublicationGuid,publicationRevision,error),error);
        {Engine::Editor::PrefabSourcePublication publication;Require(publication.Prepare(recoveryProject,prefabRegistry,publicationRevision,recoveryNext,error),error);}
        std::filesystem::current_path(fixture_/"Unrelated");
        Require(ConfigureStartupScene(scene,error),error);
        {std::ofstream invalid(fixture_/"invalid.scene",std::ios::binary);invalid<<"{\"text\":\"\xff\"}";Require(bool(invalid),"Invalid Editor Scene fixture write failed");}
        Require(!ConfigureStartupScene(fixture_/"invalid.scene",error),"Editor accepted malformed UTF-8 startup Scene");
        SetEditorMode(true);SetMcpAtStartup(true); Game::Initialize();
        // The separate Asset Browser mode covers the worker. Keep this fixture's
        // frame-indexed document commands independent of background scan timing.
        Engine::Scene::GameApplicationChecksAccess::HideAssetBrowser(*this);
        Require(HasPendingPrefabRecovery() && !IsSceneDirty(),"Actual Editor missed startup pending transactions");
        const auto recoverySceneBytes=fixtureRead(originalScene_);const auto recoverySelection=GetEditorSelection();Engine::Scene::EntityId pendingCreated=999;
        std::filesystem::path refusedPrefab="unchanged";
        Require(!CreateEditorEntity("Blocked recovery",0,pendingCreated,error) && pendingCreated==999 && !InstantiateEditorPrefab(prefabGuid_,0,pendingCreated,error) &&
            !SaveEditorPrefab("blocked.prefab",refusedPrefab,error) && refusedPrefab==std::filesystem::path("unchanged") && !UnlinkEditorPrefab(1,error) && !RevertEditorPrefab(1,error) && !RequestEditorPrefabApply(1,error) &&
            !DuplicateEditorSelection(error) && !DeleteEditorSelection(error) && !ReparentEditorSelection(0,false,error) && !AlignEditorSelectionToWorldGrid(1,error) &&
            !ApplyEditorHistory(false,error) && !ApplyEditorHistory(true,error) && !SaveSceneAs(originalScene_,error,true) && !RequestOpenScene(alternateScene_,error) && !RequestNewScene(error) && !SaveExportSettings(),
            "Actual Editor pending source guard allowed authoring/save/document/settings mutation");
        Require(GetEditorSelection()==recoverySelection && !IsSceneDirty() && fixtureRead(originalScene_)==recoverySceneBytes && !std::filesystem::exists(fixture_/"Assets/blocked.prefab"),"Rejected pending commands changed Scene/selection/files");
        Require(!CompleteEditorPrefabRecovery(error) && InspectEditorPrefabRecovery(prefabGuid_,error) && RestoreEditorPrefabRecovery(error),"History source recovery inspection/restore failed");
        auto diskChanged=Engine::Scene::SceneValue::parse(recoverySceneBytes.substr(3));diskChanged["entities"][size_t(0)]["name"]="External saved Scene owner";
        {std::ofstream out(originalScene_,std::ios::binary);out<<diskChanged.dump();out.close();Require(!out.fail(),"External Scene fixture write failed");}
        Require(!CompleteEditorPrefabRecovery(error) && HasPendingPrefabRecovery() && std::filesystem::is_regular_file(pendingHistory/"history.json") && fixtureRead(originalScene_)==diskChanged.dump(),"Recovery completion ignored/overwrote changed saved Scene");
        {std::ofstream out(originalScene_,std::ios::binary);out<<recoverySceneBytes;out.close();Require(!out.fail(),"Restore owned Scene fixture bytes failed");}
        Require(CompleteEditorPrefabRecovery(error) && HasPendingPrefabRecovery(),"History completion failed or ignored second pending transaction");
        Require(InspectEditorPrefabRecovery(pendingPublicationGuid,error) && RestoreEditorPrefabRecovery(error) && CompleteEditorPrefabRecovery(error) && !HasPendingPrefabRecovery(),error);
        Require(GetEditorSelection()==recoverySelection && !IsSceneDirty() && fixtureRead(originalScene_)==recoverySceneBytes && std::filesystem::is_regular_file(recoveryArchive/"pending.json"),"Recovery completion rewrote saved Scene/selection or lost archive");
        Require(Engine::Editor::SavePrefabDocument(prefabSource,prefabDefinition,error,true),error);
        const auto unknownPending=fixture_/"Assets/.kubo/prefab-transactions/unknown-owner";std::filesystem::create_directory(unknownPending);
        Require(!CreateEditorEntity("Late pending",0,pendingCreated,error) && RefreshPrefabRecovery(error) && HasPendingPrefabRecovery() && !InspectEditorPrefabRecovery("unknown-owner",error),"Late unknown pending escaped fresh command guard");
        // Manual intervention on this owned test fixture retains unknown data.
        std::filesystem::rename(unknownPending,fixture_/"retained-unknown-pending");Require(RefreshPrefabRecovery(error) && !HasPendingPrefabRecovery(),error);
        std::filesystem::path prefabSaved="unchanged";const auto selectionBeforePrefab=GetEditorSelection();
        Require(SaveEditorPrefab(L"プレハブ/保存.prefab",prefabSaved,error) && !IsSceneDirty() && GetEditorSelection()==selectionBeforePrefab,"Actual Prefab save changed Scene/selection");
        Require(std::filesystem::is_regular_file(prefabSaved) && !SaveEditorPrefab(prefabSaved,prefabSaved,error),"Actual Prefab save overwrote existing source");
        Engine::Scene::EntityId prefabRoot=777;
        Require(InstantiateEditorPrefab(prefabGuid_,2,prefabRoot,error) && prefabRoot==3 && IsSceneDirty() && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{prefabRoot},error);
        Require(SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),error);Engine::Scene::EntityWorld prefabPersisted;
        Require(prefabPersisted.Load(originalScene_,error) && prefabPersisted.Find(prefabRoot)->parent==2 && prefabPersisted.Find(prefabRoot)->prefabLink["sourceGuid"].get<crude_json::string>()==prefabGuid_,"Actual Prefab placement lost parent/source link");
        Require(RevertEditorPrefab(prefabRoot,error) && !IsSceneDirty(),"Actual unchanged Prefab Revert dirtied Scene");
        Engine::Scene::EntityId prefabExtra=999;
        Require(CreateEditorEntity("Instance-only child",prefabRoot,prefabExtra,error) && prefabExtra==4,error);
        auto changedPrefabDefinition=prefabDefinition;
        changedPrefabDefinition["world"]["entities"][size_t(0)]["name"]="Fresh source root";
        Require(Engine::Editor::SavePrefabDocument(prefabSource,changedPrefabDefinition,error,true),error);
        Require(RevertEditorPrefab(prefabRoot,error) && GetEditorSelection().empty() && IsSceneDirty(),"Actual Revert failed to prune removed child selection");
        Require(SaveSceneAs(originalScene_,error,true) && prefabPersisted.Load(originalScene_,error),error);
        Require(prefabPersisted.Find(prefabRoot) && prefabPersisted.Find(prefabRoot)->parent==2 && prefabPersisted.Find(prefabRoot)->name=="Fresh source root" && !prefabPersisted.Find(prefabExtra),"Actual Revert did not read fresh source or preserve root/parent/remove extras");
        Require(ApplyEditorHistory(false,error) && IsSceneDirty() && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{prefabExtra},"Actual Revert Undo lost deleted child selection/dirty baseline");
        Require(ApplyEditorHistory(true,error) && !IsSceneDirty() && GetEditorSelection().empty(),"Actual Revert Redo lost saved baseline/pruned selection");
        Require(ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{prefabRoot},error);
        Require(Engine::Editor::SavePrefabDocument(prefabSource,prefabDefinition,error,true) && SaveSceneAs(originalScene_,error,true),error);
        const auto applySourceBefore=fixtureRead(prefabSource);
        const auto applySceneBefore=fixtureRead(originalScene_);
        const auto applySelectionBefore=GetEditorSelection();
        Require(RequestEditorPrefabApply(prefabRoot,error) && HasPendingPrefabApply() && !IsSceneDirty(),error);
        Require(!RequestEditorPrefabApply(prefabRoot,error) && HasPendingPrefabApply(),"Second Apply request replaced confirmation");
        CancelEditorPrefabApply();
        Require(!HasPendingPrefabApply() && !ConfirmEditorPrefabApply(error) && fixtureRead(prefabSource)==applySourceBefore && fixtureRead(originalScene_)==applySceneBefore && GetEditorSelection()==applySelectionBefore && !IsSceneDirty(),"Apply cancel changed source/Scene/selection/baseline");
        Require(RequestEditorPrefabApply(prefabRoot,error) && ConfirmEditorPrefabApply(error) && !IsSceneDirty() && fixtureRead(prefabSource)==applySourceBefore,"No-op Apply changed source/baseline");
        Require(RequestEditorPrefabApply(prefabRoot,error),error);
        Require(Engine::Editor::SavePrefabDocument(prefabSource,changedPrefabDefinition,error,true),error);
        Require(!ConfirmEditorPrefabApply(error) && !HasPendingPrefabApply() && !IsSceneDirty() && GetEditorSelection()==applySelectionBefore && fixtureRead(originalScene_)==applySceneBefore,"Stale source confirmation changed Scene/selection/baseline");
        Require(Engine::Editor::SavePrefabDocument(prefabSource,prefabDefinition,error,true),error);
        const auto applyOriginalBytes=fixtureRead(prefabSource);
        Engine::Scene::EntityId applySibling=999;
        Require(InstantiateEditorPrefab(prefabGuid_,2,applySibling,error) && SaveSceneAs(originalScene_,error,true) && SelectEditorEntity(prefabRoot,false,false,error),error);
        Matrix4x4 applyPivot{};for(int axis=0;axis<4;++axis)applyPivot.m[axis][axis]=1;
        auto applyTranslated=applyPivot;applyTranslated.m[3][0]=2;
        Require(TransformEditorSelection(applyPivot,applyTranslated,0,error) && IsSceneDirty() && RequestEditorPrefabApply(prefabRoot,error),error);
        Require(SaveSceneAs(originalScene_,error,true) && !ConfirmEditorPrefabApply(error) && !IsSceneDirty() && fixtureRead(prefabSource)==applyOriginalBytes,"Apply ignored changed saved document baseline");
        Require(SelectEditorEntity(applySibling,true,false,error),error);
        const auto applyFullSelection=GetEditorSelection();
        Require(RequestEditorPrefabApply(prefabRoot,error) && ConfirmEditorPrefabApply(error) && IsSceneDirty() && !HasPendingPrefabRecovery() && GetEditorSelection()==applyFullSelection,error);
        const auto applyPublishedBytes=fixtureRead(prefabSource);
        Require(applyPublishedBytes!=applyOriginalBytes && SaveSceneAs(originalScene_,error,true) && prefabPersisted.Load(originalScene_,error),error);
        Require(prefabPersisted.Find(prefabRoot) && prefabPersisted.Find(applySibling) && prefabPersisted.Find(prefabRoot)->hasModel && prefabPersisted.Find(applySibling)->hasModel && std::abs(prefabPersisted.Find(prefabRoot)->transform.position.x-prefabPersisted.Find(applySibling)->transform.position.x)<0.0001f,"Apply did not persist matching instance transform/Model");
        Require(ApplyEditorHistory(false,error) && IsSceneDirty() && fixtureRead(prefabSource)==applyOriginalBytes && GetEditorSelection()==applyFullSelection && SaveSceneAs(originalScene_,error,true),"Native Apply Undo lost source/full selection/saved dirty baseline");
        Require(ApplyEditorHistory(true,error) && IsSceneDirty() && fixtureRead(prefabSource)==applyPublishedBytes && GetEditorSelection()==applyFullSelection && SaveSceneAs(originalScene_,error,true),"Native Apply Redo lost source/full selection/saved dirty baseline");
        Require(ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && fixtureRead(prefabSource)==applyOriginalBytes && GetEditorSelection()==applySelectionBefore && SaveSceneAs(originalScene_,error,true),"Apply test unwind lost previous placement/history");
        Require(UnlinkEditorPrefab(prefabRoot,error) && IsSceneDirty() && SaveSceneAs(originalScene_,error,true),error);
        Require(prefabPersisted.Load(originalScene_,error) && prefabPersisted.Find(prefabRoot)->prefabLink.is_null() && prefabPersisted.Find(prefabRoot)->hasModel,"Actual unlink removed baked Model or retained source link");
        Require(ApplyEditorHistory(false,error) && IsSceneDirty() && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{prefabRoot},"Actual unlink Undo lost selected root/saved dirty baseline");
        Require(ApplyEditorHistory(true,error) && !IsSceneDirty(),"Actual unlink Redo lost saved dirty baseline");
        Require(ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && GetEditorSelection()==selectionBeforePrefab && SaveSceneAs(originalScene_,error,true),error);
        const auto beforePrefabRefusal=GetEditorSelection();prefabRoot=999;
        Require(!InstantiateEditorPrefab("ffffffffffffffffffffffffffffffff",0,prefabRoot,error) && prefabRoot==999 && GetEditorSelection()==beforePrefabRefusal && !IsSceneDirty(),"Actual invalid Prefab selection changed Scene/output");
        Require(SelectEditorEntity(2,true,false,error),error);const auto beforeCreateSelection=GetEditorSelection();
        Engine::Scene::EntityId created=777;
        Require(CreateEditorEntity("Created probe",2,created,error) && created==3 && IsSceneDirty() && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{created},"Actual Editor create command failed");
        const auto createdScene=originalScene_;
        Require(SaveSceneAs(createdScene,error,true) && !IsSceneDirty(),error);Engine::Scene::EntityWorld createdSaved;
        Require(createdSaved.Load(createdScene,error) && createdSaved.Find(created)->parent==2 && createdSaved.Find(created)->name=="Created probe","Actual Editor create did not persist parent/name");
        Require(ApplyEditorHistory(false,error) && GetEditorSelection()==beforeCreateSelection && IsSceneDirty(),"Actual Editor create Undo lost full selection/saved dirty baseline");
        Require(ApplyEditorHistory(true,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{created} && !IsSceneDirty(),"Actual Editor create Redo lost saved state");
        Require(ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true) && SelectEditorEntity(1,false,false,error),error);
        const auto creatorSelection=GetEditorSelection();Engine::Scene::EntityId refused=999;
        Require(!CreateEditorEntity("Missing parent",999,refused,error) && refused==999 && GetEditorSelection()==creatorSelection && !IsSceneDirty(),"Actual Editor invalid create changed state");
        Require(AlignEditorSelectionToWorldGrid(0.5f,error) && IsSceneDirty(),"Actual Editor grid command failed to dirty off-grid Scene");
        Require(!AlignEditorSelectionToWorldGrid(0,error) && IsSceneDirty(),"Actual Editor accepted invalid grid step/cleared dirty state");
        Require(SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),error);
        Engine::Scene::EntityWorld gridSaved;Require(gridSaved.Load(originalScene_,error),error);
        Require(gridSaved.Find(1) && gridSaved.Find(1)->transform.position.x==0.5f && gridSaved.Find(1)->transform.position.y==-0.5f && gridSaved.Find(1)->transform.position.z==0.5f,"Actual Editor grid command did not persist aligned position");
        Require(AlignEditorSelectionToWorldGrid(0.5f,error) && !IsSceneDirty(),"Actual Editor repeated grid command dirtied saved Scene");
        Require(SelectEditorEntity(2,true,false,error) && GetEditorSelection().size()==2 && !IsSceneDirty(),"Actual Editor additive selection mutated authored state");
        const auto selectedBatch=GetEditorSelection();
        Require(!SelectEditorEntity(999,false,false,error) && GetEditorSelection()==selectedBatch,"Actual Editor missing selection changed existing selection");
        Require(AlignEditorSelectionToWorldGrid(0.3f,error) && IsSceneDirty(),"Actual Editor batch grid failed");
        Require(SaveSceneAs(originalScene_,error,true) && gridSaved.Load(originalScene_,error),error);
        Require(std::abs(gridSaved.Find(1)->transform.position.x-0.6f)<0.00001f && std::abs(gridSaved.Find(2)->transform.position.z+5.1f)<0.00001f,"Actual Editor batch grid did not persist both selected Entities");
        Require(SelectEditorEntity(1,false,false,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1} && !IsSceneDirty(),"Actual Editor replacement selection failed/dirty");
        Require(SelectEditorEntity(2,true,false,error) && DuplicateEditorSelection(error) && IsSceneDirty() && GetEditorSelection().size()==2,"Actual Editor forest duplicate command failed");
        const auto duplicatedIds=GetEditorSelection();duplicatedModel_=duplicatedIds.front();duplicatedCamera_=duplicatedIds.back();
        Require(ApplyEditorHistory(false,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && !IsSceneDirty(),"Actual Editor duplicate Undo lost source selection/baseline");
        Require(ApplyEditorHistory(true,error) && GetEditorSelection()==duplicatedIds && IsSceneDirty(),"Actual Editor duplicate Redo lost clone selection");
        Require(SaveSceneAs(originalScene_,error,true) && gridSaved.Load(originalScene_,error),error);
        Require(gridSaved.GetEntities().size()==4 && gridSaved.Find(duplicatedIds[0])->hasModel && gridSaved.Find(duplicatedIds[1])->hasCamera &&
            gridSaved.Find(duplicatedIds[0])->model.assetPath==gridSaved.Find(1)->model.assetPath && gridSaved.Find(duplicatedIds[1])->parent==gridSaved.Find(2)->parent,"Actual Editor forest duplicate persistence/asset references failed");
        std::array<Matrix4x4,2> reparentBefore{};
        for(size_t index=0;index<duplicatedIds.size();++index)Require(gridSaved.GetWorldMatrix(duplicatedIds[index],reparentBefore[index]),"Saved clone World matrix unavailable");
        Require(!ReparentEditorSelection(duplicatedModel_,true,error) && GetEditorSelection()==duplicatedIds && !IsSceneDirty(),"Actual Editor batch reparent accepted selected destination/mutated selection");
        Require(ReparentEditorSelection(2,true,error) && GetEditorSelection()==duplicatedIds && IsSceneDirty(),"Actual Editor batch keep-World reparent failed/selection lost");
        Require(ApplyEditorHistory(false,error) && GetEditorSelection()==duplicatedIds && !IsSceneDirty(),"Actual Editor batch reparent Undo lost saved world/selection");
        Require(ApplyEditorHistory(true,error) && GetEditorSelection()==duplicatedIds && IsSceneDirty(),"Actual Editor batch reparent Redo lost world/selection");
        Require(SaveSceneAs(originalScene_,error,true) && gridSaved.Load(originalScene_,error),error);
        for(size_t index=0;index<duplicatedIds.size();++index) {
            Matrix4x4 actual{};Require(gridSaved.Find(duplicatedIds[index])->parent==2 && gridSaved.GetWorldMatrix(duplicatedIds[index],actual),"Actual Editor saved batch parent mismatch");
            for(int row=0;row<4;++row)for(int column=0;column<4;++column)Require(std::abs(double(actual.m[row][column])-reparentBefore[index].m[row][column])<0.001,"Actual Editor batch keep-World changed saved clone pose");
        }
        Require(ReparentEditorSelection(2,true,error) && !IsSceneDirty(),"Actual Editor no-op batch reparent dirtied saved Scene");
        Matrix4x4 groupPivot{};for(int axis=0;axis<4;++axis)groupPivot.m[axis][axis]=1;
        for(int axis=0;axis<3;++axis)groupPivot.m[3][axis]=(reparentBefore[0].m[3][axis]+reparentBefore[1].m[3][axis])*0.5f;
        auto groupTranslated=groupPivot;groupTranslated.m[3][0]+=0.25f;
        Require(TransformEditorSelection(groupPivot,groupTranslated,0,error) && GetEditorSelection()==duplicatedIds && IsSceneDirty(),"Actual Editor group transform failed/selection lost");
        Require(ApplyEditorHistory(false,error) && GetEditorSelection()==duplicatedIds && !IsSceneDirty(),"Actual Editor group Undo lost selection/baseline");
        Require(ApplyEditorHistory(true,error) && GetEditorSelection()==duplicatedIds && IsSceneDirty(),"Actual Editor group Redo failed");
        Require(SaveSceneAs(originalScene_,error,true) && gridSaved.Load(originalScene_,error),error);
        for(size_t index=0;index<duplicatedIds.size();++index){Matrix4x4 actual{};Require(gridSaved.GetWorldMatrix(duplicatedIds[index],actual) && std::abs(actual.m[3][0]-reparentBefore[index].m[3][0]-0.25f)<0.0001f && gridSaved.Find(duplicatedIds[index])->parent==2,"Actual Editor group persistence lost root World delta/parent");}
        Require(TransformEditorSelection(groupTranslated,groupTranslated,0,error) && !IsSceneDirty(),"Actual Editor no-op group transform dirtied Scene");
        Require(SelectEditorEntity(0,false,false,error) && !DuplicateEditorSelection(error) && !IsSceneDirty(),"Empty Editor duplicate mutated saved Scene");
        Require(!DeleteEditorSelection(error) && !IsSceneDirty(),"Empty Editor subtree delete mutated saved Scene");
        Require(!ReparentEditorSelection(0,true,error) && !IsSceneDirty(),"Actual Editor empty batch reparent mutated Scene");
        Require(!TransformEditorSelection(groupPivot,groupTranslated,0,error) && !IsSceneDirty(),"Actual Editor empty group transform mutated Scene");
        Require(SelectEditorEntity(1,false,false,error),error);
        Require(std::filesystem::is_regular_file(fixture_/"Library/Mcp/editor-session.json"),"Initial Editor MCP session missing");
        CheckReplayPending(prefabSource,prefabDefinition);
        Require(PlayEditorScene(error) && !Engine::Scene::GameApplicationChecksAccess::Editing(*this) && !PlayEditorScene(error) &&
            !ApplyEditorHistory(false,error) && Engine::Scene::GameApplicationChecksAccess::Stop(*this,error) && !IsSceneDirty(),"Actual guarded Play/start/already-playing/history/Stop failed");
        OpenProjectSettings();
        const auto nativeSettingsPath=fixture_/"UserSettings/native-build.json";
        const Engine::Editor::NativeBuildSettings authoredNative{true,true,"C:/開発 SDK & '$","Native/日本語.vcxproj","Builds/日本語"};
        Require(Engine::Editor::NativeBuildSettingsFile::Save(nativeSettingsPath,authoredNative,error),error);
        OpenExportWindow(true);
        Require(SaveExportSettings(),"Actual Editor could not save loaded native settings");
        Engine::Editor::NativeBuildSettings roundtripNative;
        Require(Engine::Editor::NativeBuildSettingsFile::Load(nativeSettingsPath,roundtripNative,error) && roundtripNative.sdk==authoredNative.sdk && roundtripNative.project==authoredNative.project && roundtripNative.output==authoredNative.output && roundtripNative.debug && roundtripNative.native,"Actual Editor load/save lost native settings");
        auto externalNative=authoredNative;externalNative.output="Builds/external-writer";
        Require(Engine::Editor::NativeBuildSettingsFile::Save(nativeSettingsPath,externalNative,error),error);
        Require(!SaveExportSettings(),"Actual Editor overwrote externally changed native settings");
        Require(Engine::Editor::NativeBuildSettingsFile::Load(nativeSettingsPath,roundtripNative,error) && roundtripNative.output==externalNative.output,"Actual Editor conflict changed external file");
        Require(ReloadExportSettings() && SaveExportSettings(),"Actual Editor did not recover via explicit reload/save");
        Require(Engine::Editor::NativeBuildSettingsFile::Load(nativeSettingsPath,roundtripNative,error) && roundtripNative.output==externalNative.output,"Actual Editor reload/save discarded external draft");
        const auto gameColor=offscreenRenderManager->GetViewRenderTarget(false)->GetDesc();
        const auto gameDepth=offscreenRenderManager->GetViewDepthTarget(false)->GetDesc();
        Require(gameColor.Width==960 && gameColor.Height==540 && gameDepth.Width==960 && gameDepth.Height==540,"Editor Game color/depth output dimensions differed");
        const auto* settings=ImGui::FindWindowSettingsByID(ImHashStr("日本語MigrationMarker"));
        Require(settings && settings->Pos.x==13 && settings->Collapsed,"Actual ImGui manager did not load legacy Unicode layout");
        Require(ImGui::GetIO().IniFilename==nullptr,"ImGui still uses cwd/ANSI automatic disk IO");
        bool marker=true;const Engine::Editor::DebugEditorMenuItem item{"MigrationMarker",&marker};
        Engine::Editor::DebugEditorMenuConfig config;config.windowItems=&item;config.windowItemCount=1;
        imGuiManager->Begin();
        Require(ImGui::GetFontBaked()->FindGlyphNoFallback(0xf07b)!=nullptr,"Editor icon font was shadowed by invalid cwd font");
        Engine::Editor::DebugEditorManager::DrawMainMenu(config);imGuiManager->End();
        Require(!marker,"Actual window-visibility migration was not applied");
        marker=true;Engine::Editor::DebugEditorManager::SaveWindowItems(&item,1);
        std::filesystem::current_path(fixture_/"Unrelated");
        CheckOwnedTexture("Assets/色.png");
    }
    bool IsEndRequest() const override { return frames_>=56 || Game::IsEndRequest(); }
    void Update() override {
        std::string error;
        if(frames_==0){
            Engine::Base::ProjectContext project;Engine::Assets::AssetRegistry registry;Engine::Editor::ProjectPrefabRevision revision;
            Require(project.Open(manifest_,error) && registry.Scan(fixture_/"Assets",error) && Engine::Editor::ReadProjectPrefabRevision(project,registry,prefabGuid_,revision,error),error);
            auto next=revision.definition;next["world"]["entities"][size_t(0)]["name"]="Frame pending source";
            {Engine::Editor::PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error),error);}
            Require(InspectEditorPrefabRecovery(prefabGuid_,error) && HasPendingPrefabRecovery() && !IsSceneDirty(),"Frame recovery setup failed");
            const auto descriptor=Engine::Services::EditorMcpSession::Descriptor(fixture_);std::ifstream in(descriptor,std::ios::binary);
            const auto session=Engine::Scene::SceneValue::parse(std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()));
            const auto pipe=Engine::Base::PathFromUtf8(session["pipe"].get<crude_json::string>());
            pendingMcp_=std::async(std::launch::async,[this,pipe]{
                struct OwnedPipe{HANDLE value=INVALID_HANDLE_VALUE;~OwnedPipe(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} connection;
                connection.value=CreateFileW(pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);Require(connection.value!=INVALID_HANDLE_VALUE,"Pending MCP client could not connect");
                const std::string request=R"({"jsonrpc":"2.0","id":741,"method":"scene.create_entity","params":{"name":"Blocked pending MCP"}})";DWORD written=0;
                Require(WriteFile(connection.value,request.data(),static_cast<DWORD>(request.size()),&written,nullptr) && written==request.size(),"Pending MCP request write failed");pendingMcpSent_=true;
                const auto deadline=GetTickCount64()+2000;DWORD available=0;
                while(GetTickCount64()<deadline){Require(PeekNamedPipe(connection.value,nullptr,0,nullptr,&available,nullptr)!=0,"Pending MCP connection lost");if(available)break;Sleep(1);}
                Require(available!=0,"Pending MCP reply timed out");char reply[65536];DWORD count=0;Require(ReadFile(connection.value,reply,sizeof(reply),&count,nullptr)!=0,"Pending MCP reply read failed");
                return Engine::Scene::SceneValue::parse(std::string(reply,count));
            });
            const auto deadline=GetTickCount64()+1000;while(!pendingMcpSent_ && GetTickCount64()<deadline)Sleep(1);Require(pendingMcpSent_,"Pending MCP client did not submit request");
        }
        if(frames_==1) Require(!RequestOpenScene(fixture_/"invalid.scene",error) && !HasPendingDocumentAction() && GetOpenScenePath()==originalScene_,"Invalid Scene request altered live document");
        if(frames_==1)Require(RestoreEditorPrefabRecovery(error) && CompleteEditorPrefabRecovery(error) && !HasPendingPrefabRecovery(),error);
        if(frames_==2) {
            Require(RequestOpenScene(alternateScene_,error) && HasPendingDocumentAction(),error);
            Require(!AlignEditorSelectionToWorldGrid(1,error) && GetOpenScenePath()==originalScene_,"Grid command bypassed pending document lock");
            Require(!SelectEditorEntity(2,true,false,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Selection command bypassed pending document lock");
            Require(!DuplicateEditorSelection(error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Forest duplicate bypassed pending document lock");
            Require(!DeleteEditorSelection(error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Forest delete bypassed pending document lock");
            std::filesystem::path blockedSave="unchanged";Engine::Scene::EntityId blockedRoot=999;
            Require(!SaveEditorPrefab("Blocked.prefab",blockedSave,error) && blockedSave=="unchanged" && !std::filesystem::exists(fixture_/"Assets/Blocked.prefab"),"Prefab save bypassed pending document lock");
            Require(!InstantiateEditorPrefab(prefabGuid_,0,blockedRoot,error) && blockedRoot==999 && !UnlinkEditorPrefab(1,error) && !RevertEditorPrefab(1,error),"Prefab mutation bypassed pending document lock");
            Require(!ApplyEditorHistory(false,error) && !ApplyEditorHistory(true,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Editor history bypassed pending document lock/changed selection");
            Require(!ReparentEditorSelection(2,true,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Actual Editor batch reparent bypassed pending document lock");
            Matrix4x4 lockedPivot{};Require(!TransformEditorSelection(lockedPivot,lockedPivot,0,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1},"Actual Editor group transform bypassed pending document lock");
            Require(ConfirmDocumentAction(DocumentDecision::Cancel,error) && !HasPendingDocumentAction() && GetOpenScenePath()==originalScene_,"Cancelled Scene Open changed document");
        }
        if(frames_==4) {
            Require(RequestOpenScene(alternateScene_,error),error);
            WriteAlternate("not a Scene anymore");
        }
        if(frames_==6) {
            Require(RequestOpenScene(alternateScene_,error),error);
            Require(!RequestOpenScene(originalScene_,error) && HasPendingDocumentAction(),"Second Scene Open request replaced first");
        }
        if(frames_==9) {
            Require(RequestNewScene(error) && ConfirmDocumentAction(DocumentDecision::Cancel,error),error);
            Require(GetOpenScenePath()==alternateScene_,"Cancelled New changed open Scene");
        }
        if(frames_==10 || frames_==16) Require(RequestNewScene(error),error);
        if(frames_==11) {
            Require(GetOpenScenePath().empty() && IsSceneDirty(),"New Scene was not untitled/dirty");
            Require(RequestOpenScene(originalScene_,error) && ConfirmDocumentAction(DocumentDecision::Cancel,error),error);
            Require(GetOpenScenePath().empty() && IsSceneDirty(),"Dirty untitled Open cancellation lost new Scene");
            Require(!SaveSceneAs(manifest_,error,true) && GetOpenScenePath().empty() && IsSceneDirty(),"Save As replaced manifest or adopted invalid target");
            newSavedScene_=fixture_/L"Assets/場面/新規.scene";
            Require(SaveSceneAs(newSavedScene_,error) && GetOpenScenePath()==newSavedScene_ && !IsSceneDirty(),error);
            Engine::Scene::EntityWorld saved;Require(saved.Load(newSavedScene_,error) && saved.GetEntities().empty(),"Actual new empty Scene did not persist");sawSaveAs_=true;
        }
        if(frames_==12) Require(RequestOpenScene(originalScene_,error),error);
        if(frames_==14) {
            Require(!SaveSceneAs(alternateScene_,error) && GetOpenScenePath()==originalScene_,"Unapproved Save As replaced existing Scene/adopted path");
            Require(SaveSceneAs(alternateScene_,error,true) && GetOpenScenePath()==alternateScene_ && !IsSceneDirty(),error);
        }
        if(frames_==17) {
            Require(IsSceneDirty() && GetOpenScenePath().empty(),"Second New failed untitled state");
            Require(RequestOpenScene(originalScene_,error) && ConfirmDocumentAction(DocumentDecision::Discard,error),error);
        }
        if(frames_==3) {
            Require(SelectEditorEntity(duplicatedModel_,false,false,error) && SelectEditorEntity(duplicatedCamera_,true,false,error) && DeleteEditorSelection(error) && IsSceneDirty() && GetEditorSelection().empty(),"Actual Editor forest delete command/selection/dirty failed");
            const std::vector<Engine::Scene::EntityId> deletedSelection{duplicatedModel_,duplicatedCamera_};
            Require(ApplyEditorHistory(false,error) && GetEditorSelection()==deletedSelection && !IsSceneDirty(),"Actual Editor delete Undo lost full selection/saved baseline");
            Require(SelectEditorEntity(1,false,false,error) && ApplyEditorHistory(true,error) && GetEditorSelection().empty() && IsSceneDirty(),"Actual Editor delete Redo lost empty selection after transient click");
            Require(SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),error);
            Engine::Scene::EntityWorld deletedSaved;Require(deletedSaved.Load(originalScene_,error) && deletedSaved.GetEntities().size()==2 && deletedSaved.Find(1) && deletedSaved.Find(2) && !deletedSaved.Find(duplicatedModel_) && !deletedSaved.Find(duplicatedCamera_),"Actual Editor forest delete persistence removed originals/retained clones");
            Require(SelectEditorEntity(1,false,false,error),error);deletedDuplicates_=true;
            std::filesystem::current_path(fixture_/"UnrelatedAgain");
            CheckOwnedTexture("Assets/追加.png");
        }
        if(frames_==18)Require(SelectEditorEntity(2,true,false,error) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && !IsSceneDirty(),"Actual Editor group overlay selection setup failed");
        if(frames_==19) {
            Require(InstantiateEditorPrefab(prefabGuid_,2,renderedPrefabRoot_,error) && SaveSceneAs(originalScene_,error,true),error);
            Require(SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error),error);prefabRenderExpected_=true;
            const auto readSnapshot=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Require(bool(input),"Override fixture read failed");return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());};
            const auto selection=GetEditorSelection();const auto bytes=readSnapshot(originalScene_);
            Require(InspectEditorPrefabOverrides(renderedPrefabRoot_,error) && GetEditorPrefabOverrides().root==renderedPrefabRoot_ && GetEditorPrefabOverrides().entries.empty() && GetEditorSelection()==selection && !IsSceneDirty() && readSnapshot(originalScene_)==bytes,"Actual override inspection mutated fresh Scene/full selection/saved bytes");
            Matrix4x4 from{};for(int axis=0;axis<4;++axis)from.m[axis][axis]=1;auto to=from;to.m[3][0]=1;
            Require(SelectEditorEntity(renderedPrefabRoot_,false,false,error) && TransformEditorSelection(from,to,0,error) && InspectEditorPrefabOverrides(renderedPrefabRoot_,error) && !GetEditorPrefabOverrides().entries.empty() && GetEditorPrefabOverrides().entries.front().path=="/transform/position",error);
            CheckFieldRevert(renderedPrefabRoot_,GetEditorPrefabOverrides().entries.front());
            Require(ApplyEditorHistory(false,error) && !IsSceneDirty() && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && GetEditorSelection()==selection && readSnapshot(originalScene_)==bytes,"Inspection changed Undo/full selection/saved baseline");
            Require(RequestEditorPrefabApply(renderedPrefabRoot_,error),error);
        }
        if(frames_==20){CancelEditorPrefabApply();Require(UnlinkEditorPrefab(renderedPrefabRoot_,error) && SaveSceneAs(originalScene_,error,true),error);Require(!InspectEditorPrefabOverrides(renderedPrefabRoot_,error) && GetEditorPrefabOverrides().root==renderedPrefabRoot_ && !GetEditorPrefabOverrides().entries.empty() && !IsSceneDirty(),"Rejected unlinked refresh replaced snapshot or dirtied Scene");}
        if(frames_==21)Require(ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true),error);
        if(frames_==22) {Require(ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true),error);prefabRenderExpected_=false;}
        if(frames_==23) {
            Require(InstantiateEditorPrefab(prefabGuid_,2,renderedPrefabRoot_,error) && SaveSceneAs(originalScene_,error,true),error);prefabRenderExpected_=true;
            Matrix4x4 from{};for(int axis=0;axis<4;++axis)from.m[axis][axis]=1;auto to=from;to.m[3][0]=1;
            Require(TransformEditorSelection(from,to,0,error) && InspectEditorPrefabOverrides(renderedPrefabRoot_,error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && RequestEditorPrefabFieldRevert(renderedPrefabRoot_,GetEditorPrefabOverrides().entries.front(),error),error);
        }
        if(frames_==24)Require(ConfirmEditorPrefabFieldRevert(error) && !IsSceneDirty() && !HasPendingPrefabFieldRevert() && SaveSceneAs(originalScene_,error,true),error);
        if(frames_==25)CheckFieldApply(renderedPrefabRoot_);
        if(frames_==26)PrepareRenderedFieldConflict(true,error);
        if(frames_==27)Require(ConfirmEditorPrefabApply(error,true) && !HasPendingPrefabApply() && ApplyEditorHistory(false,error) && ApplyEditorHistory(true,error) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && SaveSceneAs(originalScene_,error,true),error);
        if(frames_==29)PrepareRenderedFieldConflict(false,error);
        if(frames_==30)Require(ConfirmEditorPrefabFieldRevert(error,true) && !HasPendingPrefabFieldRevert() && ApplyEditorHistory(false,error) && ApplyEditorHistory(true,error) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && SaveSceneAs(originalScene_,error,true),error);
        if(frames_==31)PrepareRenderedAdoption(error);
        if(frames_==32)FinishRenderedAdoption(error);
        if(frames_==33){
            const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
            Require(Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(adoptionSource_),error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
            const auto& entries=GetEditorPrefabThreeWay()->entries;const auto row=std::find_if(entries.begin(),entries.end(),[](const auto& entry){return entry.path=="/transform/rotation";});Require(row!=entries.end(),"Reopen adoption row missing");
            Require(RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,*row,error) && RequestOpenScene(originalScene_,error),error);
        }
        if(frames_==34){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(GetOpenScenePath()==originalScene_ && !Access::ComparisonSessionMatches(*this) && !Access::OverrideSessionMatches(*this) && !ConfirmEditorPrefabFieldAdoption(error) && !HasPendingPrefabFieldAdoption() && !IsSceneDirty(),"Same-path reopen retained prior snapshot/confirmation session");
            Require(InspectEditorPrefabThreeWay(renderedPrefabRoot_,error) && Access::ComparisonSessionMatches(*this) && RestoreRenderedConflictSource(error),error);
            std::cout<<"PASS: actual same-path identical Scene reopen invalidates retained comparison/override session and pending adoption; fresh comparison restores binding\n";
        }
        if(frames_==35){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto history=Access::History(*this);const auto before=Access::World(*this);
            const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
            for(const auto& lockedPath:std::vector<std::filesystem::path>{source,Engine::Assets::AssetRegistry::MetadataPath(source)}){
                const HANDLE writer=CreateFileW(lockedPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                Require(writer!=INVALID_HANDLE_VALUE,"Root Revert writer fixture open failed");const bool accepted=RevertEditorPrefab(renderedPrefabRoot_,error);CloseHandle(writer);
                Require(!accepted && Access::World(*this)==before && Access::History(*this).HasSameState(history) && !IsSceneDirty() && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2},"Root Revert accepted native source/metadata writer or changed authoring on failure");
            }
            Require(!RequestEditorPrefabRevert(1,error) && !HasPendingPrefabRevert() && RequestEditorPrefabRevert(renderedPrefabRoot_,error) && HasPendingPrefabRevert() && !RequestEditorPrefabApply(renderedPrefabRoot_,error),"Root Revert request accepted unlinked root or omitted exclusion");
            CancelEditorPrefabRevert();Require(!HasPendingPrefabRevert() && !ConfirmEditorPrefabRevert(error) && Access::World(*this)==before && Access::History(*this).HasSameState(history) && !IsSceneDirty(),"Root Revert preview/cancel changed authoring");
            auto changed=Engine::Editor::PrefabValue::parse(renderedConflictSource_);changed["world"]["entities"][size_t(0)]["transform"]["rotation"]=crude_json::array{0.0,1.0,0.0};
            Require(Engine::Editor::SavePrefabDocument(fixture_/L"Assets/プレハブ/モデル.prefab",changed,error,true) && RequestEditorPrefabRevert(renderedPrefabRoot_,error),error);
        }
        if(frames_==36){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";const auto bytes=ReadAdoptionFixture(source);const auto saved=ReadAdoptionFixture(originalScene_);const auto before=Access::World(*this);
            Require(ConfirmEditorPrefabRevert(error) && !HasPendingPrefabRevert() && !ConfirmEditorPrefabRevert(error) && IsSceneDirty() && ReadAdoptionFixture(source)==bytes && ReadAdoptionFixture(originalScene_)==saved && ApplyEditorHistory(false,error) && Access::World(*this)==before && !IsSceneDirty() && ApplyEditorHistory(true,error) && IsSceneDirty() && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error),"Root Revert confirmation/source isolation/Undo/Redo failed");
        }
        if(frames_==37)Require(RequestEditorPrefabRevert(renderedPrefabRoot_,error) && RequestOpenScene(originalScene_,error),error);
        if(frames_==38){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto before=Access::World(*this);const auto history=Access::History(*this);const auto saved=ReadAdoptionFixture(originalScene_);const auto bytes=ReadAdoptionFixture(fixture_/L"Assets/プレハブ/モデル.prefab");
            Require(GetOpenScenePath()==originalScene_ && !ConfirmEditorPrefabRevert(error) && !HasPendingPrefabRevert() && Access::World(*this)==before && Access::History(*this).HasSameState(history) && !IsSceneDirty() && ReadAdoptionFixture(originalScene_)==saved && ReadAdoptionFixture(fixture_/L"Assets/プレハブ/モデル.prefab")==bytes,"Root Revert survived identical same-path reopen or changed files/history");
            std::cout<<"PASS: actual root Prefab Revert source/metadata writer refusal, preview/cancel/exclusion/single-use confirmation/source and saved Scene isolation/Undo/Redo, identical same-path reopen refusal and retained modal rendering\n";
        }
        if(frames_==39){
            using Access=Engine::Scene::GameApplicationChecksAccess;PrepareRenderedFieldConflict(true,error,true);
            const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";const auto bytes=ReadAdoptionFixture(source);const auto before=Access::World(*this);const auto history=Access::History(*this);const auto saved=ReadAdoptionFixture(originalScene_);
            Require(GetPendingPrefabApplySourceReview() && GetPendingPrefabApplySourceReview()->RequiresApproval() && !ConfirmEditorPrefabApply(error) && !HasPendingPrefabApply() && Access::World(*this)==before && Access::History(*this).HasSameState(history) && ReadAdoptionFixture(source)==bytes && ReadAdoptionFixture(originalScene_)==saved,"Unapproved whole Apply published conflicting source/Scene/history");
            Require(RequestEditorPrefabApply(renderedPrefabRoot_,error),error);auto changed=Engine::Editor::PrefabValue::parse(bytes);changed["world"]["entities"][size_t(0)]["name"]="Changed after whole review";
            Require(Engine::Editor::SavePrefabDocument(source,changed,error,true) && !ConfirmEditorPrefabApply(error,true) && Access::World(*this)==before && Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(bytes),error,true) && RequestEditorPrefabApply(renderedPrefabRoot_,error),"Whole replacement approval bypassed native revision check");
        }
        if(frames_==40){
            Require(ConfirmEditorPrefabApply(error,true) && !HasPendingPrefabApply() && ApplyEditorHistory(false,error) && ApplyEditorHistory(true,error) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && SaveSceneAs(originalScene_,error,true),error);
            std::cout<<"PASS: actual whole Apply source replacement review, default refusal/source and Scene/history isolation, explicit approval/native Undo/Redo, stale source refusal and rendered replacement modal\n";
        }
        if(frames_==42){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
            Require(Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(adoptionSource_),error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
            const auto& entries=GetEditorPrefabThreeWay()->entries;const auto row=std::find_if(entries.begin(),entries.end(),[](const auto& entry){return entry.local==2 && entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(row!=entries.end(),"Actual addition row missing");
            entityAdditionWorld_=Access::World(*this);const auto history=Access::History(*this);
            Require(!RequestEditorPrefabEntityAddition(renderedPrefabRoot_,"wrong-guid",*row,error) && RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,*row,error) && HasPendingPrefabEntityAddition() && !HasPendingPrefabFieldAdoption() && !ConfirmEditorPrefabFieldRevert(error,true) && HasPendingPrefabEntityAddition() && !RequestEditorPrefabApply(renderedPrefabRoot_,error),"Entity addition omitted GUID/type/mutual confirmation checks");
            CancelEditorPrefabFieldRevert();Require(Access::World(*this)==entityAdditionWorld_ && Access::History(*this).HasSameState(history) && !IsSceneDirty() && RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,*row,error) && Access::StructuralPreviewMatchesCore(*this,error),"Entity addition preview/cancel or core equivalence failed");
        }
        if(frames_==43){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";const auto bytes=ReadAdoptionFixture(source);const auto saved=ReadAdoptionFixture(originalScene_);
            Require(ConfirmEditorPrefabEntityAddition(error) && !HasPendingPrefabEntityAddition() && !ConfirmEditorPrefabEntityAddition(error) && IsSceneDirty(),"Entity addition single-use publication failed");const auto after=Access::World(*this);
            Require(Engine::Editor::PrefabValue::parse(after)["entities"].get<crude_json::array>().size()==Engine::Editor::PrefabValue::parse(entityAdditionWorld_)["entities"].get<crude_json::array>().size()+1 && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && ReadAdoptionFixture(source)==bytes && ReadAdoptionFixture(originalScene_)==saved,"Entity addition imported unselected children or changed selection/files");
            entityAdditionAfterWorld_=after; // Keep the adopted Model alive through Update/Draw before replay.
        }
        if(frames_==44){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(Access::World(*this)==entityAdditionAfterWorld_ && ApplyEditorHistory(false,error) && Access::World(*this)==entityAdditionWorld_ && !IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==entityAdditionAfterWorld_ && IsSceneDirty() && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error),"Entity addition Undo/Redo/fixture restore failed");
            std::cout<<"PASS: actual source-only Entity addition held preview/cancel/GUID/mutual/type checks, single-use selected-only addition/source and saved Scene isolation/full selection/Undo/Redo/dirty and rendered modal\n";
        }
        if(frames_==45)PrepareRenderedAncestorAddition(error);
        if(frames_==46){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(ConfirmEditorPrefabEntityAddition(error) && !HasPendingPrefabEntityAddition() && IsSceneDirty(),"Ancestor addition confirmation failed");
            entityAncestorAfter_=Access::World(*this);Engine::Scene::EntityWorld checked;Require(checked.Restore(Engine::Editor::PrefabValue::parse(entityAncestorAfter_),error),error);
            Require(checked.GetEntities().size()==Engine::Editor::PrefabValue::parse(entityAncestorBefore_)["entities"].get<crude_json::array>().size()+2 && checked.Find(entityAncestorParent_) && checked.Find(entityAncestorChild_) && checked.Find(entityAncestorParent_)->parent==renderedPrefabRoot_ && checked.Find(entityAncestorChild_)->parent==entityAncestorParent_ && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && ReadAdoptionFixture(fixture_/L"Assets/プレハブ/モデル.prefab")==entityAncestorSource_ && ReadAdoptionFixture(originalScene_)==entityAncestorSaved_,"Ancestor addition imported unrelated nodes or changed hierarchy/full selection/files");
        }
        if(frames_==47){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(sawEntityAncestorModels_ && Access::World(*this)==entityAncestorAfter_ && ApplyEditorHistory(false,error) && Access::World(*this)==entityAncestorBefore_ && !IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==entityAncestorAfter_ && IsSceneDirty() && ApplyEditorHistory(false,error) && !IsSceneDirty() && RestoreRenderedConflictSource(error),"Ancestor addition rendering/replay/fixture restore failed");
            std::cout<<"PASS: actual Entity addition missing-new-parent closure, stale source/selection/pending document/source and metadata writer refusal with World/high-water/history/document/file isolation, retained parent/child Model rendering and full-selection Undo/Redo\n";
        }
        if(frames_==48)PrepareRenderedEntityRemoval(error);
        if(frames_==49){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(ConfirmEditorPrefabEntityRemoval(error,true) && !HasPendingPrefabEntityRemoval() && !ConfirmEditorPrefabEntityRemoval(error,true) && IsSceneDirty(),"Approved removal single-use confirmation failed");
            entityRemovalAfter_=Access::World(*this);Engine::Scene::EntityWorld checked;Require(checked.Restore(Engine::Editor::PrefabValue::parse(entityRemovalAfter_),error),error);
            Require(!checked.Find(entityRemovalParent_) && !checked.Find(entityRemovalChild_) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1} && ReadAdoptionFixture(fixture_/L"Assets/プレハブ/モデル.prefab")==entityRemovalSource_ && ReadAdoptionFixture(originalScene_)==entityRemovalSaved_,"Removal did not prune hierarchy/selection or modified source/saved Scene");
        }
        if(frames_==50){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(sawRemovedModelEntries_ && ApplyEditorHistory(false,error) && Access::World(*this)==entityRemovalBefore_ && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,entityRemovalChild_} && !IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==entityRemovalAfter_ && IsSceneDirty() && SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),"Removal Undo/Redo/save lost full selection/dirty baseline");
            Require(ApplyEditorHistory(false,error) && IsSceneDirty() && ApplyEditorHistory(true,error) && !IsSceneDirty() && ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true) && RestoreRenderedConflictSource(error),"Removal saved-baseline replay failed");
            std::cout<<"PASS: actual Entity removal descendant conflict review/default refusal/explicit approval, GUID/type/cancel/native revision/selection/document guards, single-use removal/pruned selection/source and saved Scene isolation, Model-entry removal and full-selection saved-baseline Undo/Redo\n";
        }
        if(frames_==51)PrepareRenderedEntityRestoration(error);
        if(frames_==52){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(ConfirmEditorPrefabEntityRestoration(error,true) && !HasPendingPrefabEntityRestoration() && !ConfirmEditorPrefabEntityRestoration(error,true) && IsSceneDirty(),"Restoration approved single-use publication failed");restorationAfter_=Access::World(*this);
            Engine::Scene::EntityWorld checked;Require(checked.Restore(Engine::Editor::PrefabValue::parse(restorationAfter_),error) && checked.Find(entityRemovalParent_) && checked.Find(entityRemovalChild_) && checked.Find(entityRemovalChild_)->parent==entityRemovalParent_ && checked.Find(entityRemovalParent_)->name=="Changed deleted source parent" && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && ReadAdoptionFixture(fixture_/L"Assets/プレハブ/モデル.prefab")==restorationSource_ && ReadAdoptionFixture(originalScene_)==restorationSaved_,"Restoration changed IDs/selection/files or did not restore required parent");
        }
        if(frames_==53){
            using Access=Engine::Scene::GameApplicationChecksAccess;
            Require(sawRestoredModels_ && ApplyEditorHistory(false,error) && Access::World(*this)==restorationBefore_ && !IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==restorationAfter_ && IsSceneDirty() && SaveSceneAs(originalScene_,error,true) && !IsSceneDirty() && ApplyEditorHistory(false,error) && IsSceneDirty() && ApplyEditorHistory(true,error) && !IsSceneDirty() && ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true),"Restoration full-selection/saved dirty-baseline replay failed");
            Require(InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);const auto& rows=GetEditorPrefabThreeWay()->entries;const auto row=std::find_if(rows.begin(),rows.end(),[](const auto& entry){return entry.local==3 && entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(row!=rows.end() && RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,*row,error) && RequestOpenScene(originalScene_,error),"Restoration same-path reopen setup failed");
        }
        if(frames_==54){
            using Access=Engine::Scene::GameApplicationChecksAccess;const auto before=Access::World(*this);const auto history=Access::History(*this);const auto document=Access::Document(*this);
            Require(before==restorationBefore_ && !ConfirmEditorPrefabEntityRestoration(error,true) && !HasPendingPrefabEntityRestoration() && Access::World(*this)==before && Access::History(*this).HasSameState(history) && Access::Document(*this).HasSameState(document) && !IsSceneDirty() && RestoreRenderedConflictSource(error),"Restoration approved identical same-path reopen or changed state");
            std::cout<<"PASS: actual mapped Entity restoration changed-parent conflict/default refusal/approval, GUID/type/cancel/selection/source/native writer/session guards, stable parent/child IDs, Model-entry restoration and full-selection saved-baseline Undo/Redo\n";
        }
        if(frames_>=26 && frames_<=31){const auto filter=frames_==26?1:frames_==27?2:frames_==28?3:frames_==29?4:0;Engine::Scene::GameApplicationChecksAccess::SetThreeWayView(*this,filter,frames_==30?999:0);}
        Game::Update();
        if(frames_==33 || frames_==37 || frames_==53)Require(SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error),error);
        if(frames_==0){Require(pendingMcp_.wait_for(std::chrono::milliseconds(100))==std::future_status::ready,"Actual pending MCP response did not arrive");const auto response=pendingMcp_.get();Require(response.contains("error") && !IsSceneDirty() && HasPendingPrefabRecovery(),"Pending source MCP mutation succeeded/changed document");}
        if(frames_==4) {
            Require(GetOpenScenePath()==originalScene_ && !HasPendingDocumentAction() && std::filesystem::is_regular_file(fixture_/"Library/Mcp/editor-session.json"),"Revalidation failure replaced old Scene or stopped MCP");
            WriteAlternate(alternateBytes_);
        }
        if(frames_==6) {Require(GetOpenScenePath()==alternateScene_ && !HasPendingDocumentAction() && !IsSceneDirty() && !std::filesystem::exists(fixture_/"Library/Mcp/editor-session.json") && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{3},"Successful Scene Open failed path/dirty/pending/MCP/selection lifecycle");activeModel_=3;}
        if(frames_==10 || frames_==16) {Require(GetOpenScenePath().empty() && IsSceneDirty() && !HasPendingDocumentAction() && GetEditorSelection().empty(),"New Scene failed path/dirty/pending/selection state");activeModel_=0;}
        if(frames_==12 || frames_==17) {Require(GetOpenScenePath()==originalScene_ && !IsSceneDirty(),"Open after New/Save As failed");activeModel_=1;}
        ++frames_;
    }
    void Draw() override {
        if(auto* hierarchy=ImGui::FindWindowByName("Entity Hierarchy"))hierarchy->StateStorage.SetInt(hierarchy->GetID("Prefab assets"),1);
        if(auto* overrides=ImGui::FindWindowByName("Prefab Overrides"))for(int index=0;index<static_cast<int>(GetEditorPrefabOverrides().entries.size());++index)
            overrides->StateStorage.SetInt(ImHashStr("difference",0,overrides->GetID(index)),1);
        if(auto* comparison=ImGui::FindWindowByName("Prefab Three-Way"))if(const auto* report=GetEditorPrefabThreeWay())for(int index=0;index<static_cast<int>(report->entries.size());++index)
            comparison->StateStorage.SetInt(ImHashStr("comparison",0,comparison->GetID(index)),1);
        Game::Draw(); ++draws_;
        if(frames_>=20){const auto* overrides=ImGui::FindWindowByName("Prefab Overrides");Require(overrides && overrides->Active && !GetEditorPrefabOverrides().entries.empty(),"Actual override snapshot window was not rendered");}
        if(frames_>=26){const auto* comparison=ImGui::FindWindowByName("Prefab Three-Way");Require(comparison && comparison->Active && GetEditorPrefabThreeWay() && GetEditorPrefabThreeWay()->entries.size()>64,"Actual full three-way comparison window was not rendered");}
        if(frames_==31)Require(Engine::Scene::GameApplicationChecksAccess::ThreeWayView(*this)==std::pair{0,1},"Actual comparison page was not clamped to the second page");
        if(frames_==20){const auto* applyWindow=ImGui::FindWindowByName("Confirm Prefab Apply");Require(applyWindow && applyWindow->Active && HasPendingPrefabApply(),"Actual Apply confirmation modal was not rendered");sawApplyModal_=true;}
        if(frames_==24){const auto* fieldWindow=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(fieldWindow && fieldWindow->Active && HasPendingPrefabFieldRevert(),"Actual field Revert confirmation modal was not rendered");sawFieldModal_=true;}
        if(frames_==27){const auto* fieldWindow=ImGui::FindWindowByName("Confirm Prefab Field Apply");Require(fieldWindow && fieldWindow->Active && HasPendingPrefabApply() && GetPendingPrefabFieldApplyChange()==Engine::Editor::PrefabThreeWayChange::Conflict,"Actual field Apply conflict confirmation modal was not rendered");sawFieldApplyModal_=true;}
        if(frames_==30){const auto* fieldWindow=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(fieldWindow && fieldWindow->Active && HasPendingPrefabFieldRevert() && GetPendingPrefabFieldRevertChange()==Engine::Editor::PrefabThreeWayChange::Conflict,"Actual field Revert conflict confirmation modal was not rendered");sawConflictRevertModal_=true;}
        if(frames_==32){const auto* fieldWindow=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(fieldWindow && fieldWindow->Active && HasPendingPrefabFieldAdoption() && GetPendingPrefabFieldRevertChange()==Engine::Editor::PrefabThreeWayChange::SourceOnly,"Actual source field adoption confirmation modal was not rendered");sawAdoptionModal_=true;}
        if(frames_==36 || frames_==38){const auto* revert=ImGui::FindWindowByName("Confirm Prefab Revert");Require(revert && revert->Active && HasPendingPrefabRevert(),"Actual root Revert confirmation modal was not rendered");sawRootRevertModal_=true;}
        if(frames_==40){const auto* apply=ImGui::FindWindowByName("Confirm Prefab Apply");Require(apply && apply->Active && GetPendingPrefabApplySourceReview() && GetPendingPrefabApplySourceReview()->RequiresApproval(),"Actual whole Apply replacement confirmation was not rendered");sawWholeConflictModal_=true;}
        if(frames_==43){const auto* modal=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(modal && modal->Active && HasPendingPrefabEntityAddition(),"Actual Entity addition confirmation modal was not rendered");sawEntityAdditionModal_=true;}
        if(frames_==46){const auto* modal=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(modal && modal->Active && HasPendingPrefabEntityAddition(),"Actual ancestor addition modal was not rendered");sawEntityAncestorModal_=true;}
        if(frames_==49){const auto* modal=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(modal && modal->Active && HasPendingPrefabEntityRemoval() && GetPendingPrefabEntityRemovals() && GetPendingPrefabEntityRemovals()->size()==2,"Actual removal closure modal was not rendered");sawEntityRemovalModal_=true;}
        if(frames_==52){const auto* modal=ImGui::FindWindowByName("Confirm Prefab Field Revert");Require(modal && modal->Active && HasPendingPrefabEntityRestoration() && GetPendingPrefabEntityRestorations() && GetPendingPrefabEntityRestorations()->size()==2,"Actual restoration dependency modal was not rendered");sawRestorationModal_=true;}
        auto* hierarchy=ImGui::FindWindowByName("Entity Hierarchy");
        Require(hierarchy && hierarchy->Active,"Actual Prefab Hierarchy window was not rendered");
        if(draws_>1)Require(hierarchy->StateStorage.GetInt(hierarchy->GetID("Prefab assets"))==1,"Actual Prefab controls were not expanded for rendering");
        if(frames_>=19 && frames_<49)Require(GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && IsSceneDirty()==(frames_==24 || frames_==27 || frames_==30 || frames_==40 || frames_==44 || frames_==47),"Actual Editor multi-selection/field-preview overlay changed selection/Scene");
        const auto* settingsWindow=ImGui::FindWindowByName("Project Settings");
        Require(settingsWindow && settingsWindow->Active,"Actual Project Settings window was not rendered");
        const auto* exportWindow=ImGui::FindWindowByName("Export Game");
        Require(exportWindow && exportWindow->Active,"Actual native export window was not rendered");
        const auto* recoveryWindow=ImGui::FindWindowByName("Prefab Recovery");
        Require(recoveryWindow && recoveryWindow->Active,"Actual Prefab Recovery window was not rendered");
        if(frames_==1)Require(HasPendingPrefabRecovery() && !IsSceneDirty(),"Rendered pending frame lost authoring guard/dirty baseline");
        Require(GetGameOutputExtent()==Engine::Base::ViewExtent{960,540},"Opening settings changed live output without restart");
        Require(ImGui::DockBuilderGetNode(0x12345671) && ImGui::DockBuilderGetNode(0x12345672),"Default dock builder replaced restored custom layout");
        auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
        if(frames_==47){Require(scene && scene->GetModelRenderStatus(entityAncestorParent_).loaded && scene->GetModelRenderStatus(entityAncestorChild_).loaded,"Actual adopted ancestor/child Models did not reconcile into render entries");sawEntityAncestorModels_=true;}
        if(frames_==50){Require(scene && !scene->GetModelRenderStatus(entityRemovalParent_).loaded && !scene->GetModelRenderStatus(entityRemovalChild_).loaded,"Actual removed Models retained live render entries");sawRemovedModelEntries_=true;}
        if(frames_==53){Require(scene && scene->GetModelRenderStatus(entityRemovalParent_).loaded && scene->GetModelRenderStatus(entityRemovalChild_).loaded,"Restored parent/child Model entries did not load");sawRestoredModels_=true;}
        if(activeModel_==1 && duplicatedModel_ && duplicatedModel_!=renderedPrefabRoot_)Require(scene && scene->GetModelRenderStatus(duplicatedModel_).loaded!=deletedDuplicates_,"Actual Editor duplicate/delete did not reconcile clone model render entry");
        if(renderedPrefabRoot_)Require(scene && scene->GetModelRenderStatus(renderedPrefabRoot_).loaded==prefabRenderExpected_,"Actual Prefab placement/unlink/Undo failed to reconcile model render entry");
        if(activeModel_==3) {
            Require(scene && scene->GetModelRenderStatus(3).loaded && !scene->GetModelRenderStatus(1).loaded,"Actual Scene Open did not reconcile new/old model IDs");
            sawOpenedScene_=true;
        }
        if(activeModel_==0) {Require(scene && !scene->GetModelRenderStatus(1).loaded && !scene->GetModelRenderStatus(3).loaded,"New Scene kept old model render entries");sawNewScene_=true;}
        if (scene && activeModel_!=0 && scene->GetModelRenderStatus(activeModel_).loaded && offscreenRenderManager->HasEditorView()) {
            const auto extent=offscreenRenderManager->GetEditorViewExtent();
            sawResize_=sawResize_ || extent!=Engine::Base::ViewExtent{};
            const auto projection=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->GetProjectionMatrix();
            Require(std::abs(projection.m[1][1]/projection.m[0][0]-extent.Aspect())<0.0001f,"Actual Scene panel/camera aspect differs");
            sawViews_=true;
        }
    }
    void Verify() {
        Require(draws_==55 && sawViews_ && sawResize_ && sawOpenedScene_ && sawNewScene_ && sawSaveAs_ && sawApplyModal_ && sawFieldModal_ && sawFieldApplyModal_ && sawConflictRevertModal_ && sawAdoptionModal_ && sawRootRevertModal_ && sawWholeConflictModal_ && sawEntityAdditionModal_ && sawEntityAncestorModal_ && sawEntityAncestorModels_ && sawEntityRemovalModal_ && sawRemovedModelEntries_ && sawRestorationModal_ && sawRestoredModels_ && !offscreenRenderManager->HasEditorView() && !offscreenRenderManager->GetViewRenderTarget(true),"Actual generic Editor dual-view/debounced resize/Open/New/Save As/Prefab modals/finalization failed");
        std::cout<<"PASS: actual Editor read-only Prefab baseline snapshot, fresh/changed Transform reports, saved bytes/full selection/Undo preservation, rejected unlinked refresh and expanded snapshot window rendering (not physical gestures/pixel QA)\n";
        const Engine::Base::ProjectPaths paths(fixture_);std::string text,error;
        {std::ifstream input(manifest_,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};Require(bytes==manifestBytes_,"Opening/rendering Project Settings rewrote manifest");}
        Require(Engine::Editor::EditorSettingsFile::Read(paths.EditorLayout(),{},text,error) && text.find("日本語MigrationMarker")!=std::string::npos && text.find("0x12345671")!=std::string::npos,"Actual Editor shutdown did not persist migrated layout/dock nodes");
        Require(Engine::Editor::EditorSettingsFile::Read(fixture_/"imgui.ini",{},text,error) && text==legacyLayout_,"Editor modified legacy layout source");
        Require(Engine::Editor::EditorSettingsFile::Read(paths.EditorWindows(),{},text,error) && text.find("MigrationMarker=1")!=std::string::npos,"Actual visibility save did not use UserSettings");
#ifdef _DEBUG
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue))),"DX12 debug queue is unavailable");
        for (UINT64 index=0; index<queue->GetNumStoredMessages(); ++index) {
            SIZE_T size=0; queue->GetMessage(index,nullptr,&size); std::vector<unsigned char> storage(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(queue->GetMessage(index,message,&size))) Require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);
        }
#endif
        std::cout<<"PASS: actual ImGui Unicode legacy layout/visibility load, custom dock preservation, UserSettings save and unchanged legacy source\n";
        std::cout<<"PASS: actual generic Editor project-relative model rendering survives two unrelated cwd changes\n";
        std::cout<<"PASS: actual project-owned Unicode GPU texture loads after two cwd changes, same-name competitor isolation and absolute/relative SRV aliases\n";
        std::cout<<"PASS: actual Editor merges installed icon glyph despite invalid cwd font and initializes shaders from unrelated cwd\n";
        std::cout<<"PASS: actual Editor configured Game color/depth output independent of panel-driven Scene target\n";
        std::cout<<"PASS: actual Project Settings window renders across 24 frames without changing manifest or live output\n";
        std::cout<<"PASS: actual SDK native export window renders across 24 frames beside Scene/Game and document lifecycle operations\n";
        std::cout<<"PASS: actual Editor startup history/publication recovery, saved Scene/selection preservation, archive completion and stale-history reset, late unknown guards and rendered pending Recovery window with live MCP mutation refusal (fixture marker, not physical confirmation/Editor crash QA)\n";
        std::cout<<"PASS: actual Editor Unicode native settings load/save, external-change refusal and explicit reload/retry without starting a build\n";
        std::cout<<"PASS: actual Editor Unicode Scene Open/cancel/revalidation failure, preserved old MCP until successful switch, new model rendering/old ID removal and unchanged startup manifest\n";
        std::cout<<"PASS: actual Editor New/Cancel, untitled dirty rendering/model cleanup, Unicode Save As/reopen, manifest/unapproved overwrite rejection, approved Scene replacement and dirty Discard Open\n";
        std::cout<<"PASS: actual Editor world grid command, half-step authored position persistence, dirty/no-op state and invalid-step/pending-document refusal (not physical button QA)\n";
        std::cout<<"PASS: actual Editor multi-selection command, missing/pending refusal, two-Entity grid persistence and Scene Open/New selection reset (not physical Ctrl/Shift click QA)\n";
        std::cout<<"PASS: actual Editor forest duplication, cloned Model/Camera selection/save/render, retained asset references and empty/pending refusal (not physical Ctrl+D/button QA)\n";
        std::cout<<"PASS: actual Editor selected-forest deletion, persisted clone removal/original preservation, empty selection, dirty baseline and live model cache reconciliation (not physical Delete/button QA)\n";
        std::cout<<"PASS: actual Editor full multi-selection Undo/Redo for forest duplicate/delete, saved dirty baseline and transient-click independence (not physical shortcut/button QA)\n";
        std::cout<<"PASS: actual Editor forest reparent, saved Model/Camera parent and World pose, full-selection Undo/Redo, no-op/empty/inside-forest/pending refusal and continued dual-view rendering (not physical combo QA)\n";
        std::cout<<"PASS: actual Editor shared-pivot group command, Model/Camera World delta persistence, parents/full selection/Undo/Redo/dirty and no-op/empty/pending refusal with continued rendering (not physical drag QA)\n";
        std::cout<<"PASS: actual Editor candidate Entity creation, saved name/parent, full-selection Undo/Redo and saved dirty baseline, missing-parent refusal and continued dual-view rendering (not physical button QA)\n";
        std::cout<<"PASS: actual Editor project-owned Prefab save/no-overwrite, registered GUID placement/parent/link persistence, unlink retaining baked Model, full-selection Undo/Redo/dirty and invalid-source refusal (not physical button QA)\n";
        std::cout<<"PASS: actual Editor Apply preview/cancel/no-op, stale source/saved baseline refusal, matching instance Model/transform persistence, native source Undo/Redo/full selection/dirty and rendered confirmation modal (not physical button/propagated GPU rendering/Editor crash QA)\n";
        std::cout<<"PASS: actual Game source-only native replay success with retained unknown data and collision failure, immediate pending guard before Update/explicit refresh, same-frame command/Play/save rejection, native recovery/unknown archive preserving Scene/selection, guarded Play and continued rendering (not physical Undo/Inspector gesture QA)\n";
    }
private:
    static std::string ReadAdoptionFixture(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Require(bool(input),"Adoption fixture read failed");return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());}
    void PrepareRenderedEntityRestoration(std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
        Require(SelectEditorEntity(entityRemovalParent_,false,false,error) && DeleteEditorSelection(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && SaveSceneAs(originalScene_,error,true),error);
        auto incoming=Engine::Editor::PrefabValue::parse(adoptionSource_);incoming["world"]["entities"][size_t(2)]["parent"]=2.0;incoming["world"]["entities"][size_t(1)]["name"]="Changed deleted source parent";
        Require(Engine::Editor::SavePrefabDocument(source,incoming,error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
        const auto& rows=GetEditorPrefabThreeWay()->entries;const auto found=std::find_if(rows.begin(),rows.end(),[](const auto& entry){return entry.local==3 && entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(found!=rows.end(),"Restoration target missing");const auto row=*found;
        restorationBefore_=Access::World(*this);restorationSource_=ReadAdoptionFixture(source);restorationSaved_=ReadAdoptionFixture(originalScene_);const auto history=Access::History(*this);const auto document=Access::Document(*this);const auto high=Access::HighWater(*this);
        const auto unchanged=[&]{return Access::World(*this)==restorationBefore_ && Access::HighWater(*this)==high && Access::History(*this).HasSameState(history) && Access::Document(*this).HasSameState(document) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && ReadAdoptionFixture(source)==restorationSource_ && ReadAdoptionFixture(originalScene_)==restorationSaved_;};
        Require(!RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,"wrong-guid",row,error) && RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error) && !HasPendingPrefabFieldAdoption() && !ConfirmEditorPrefabFieldRevert(error,true) && HasPendingPrefabEntityRestoration() && !ConfirmEditorPrefabEntityRemoval(error,true) && !ConfirmEditorPrefabEntityAddition(error) && !RequestEditorPrefabApply(renderedPrefabRoot_,error),"Restoration type/GUID/mutual guard failed");
        const auto* dependencies=GetPendingPrefabEntityRestorations();Require(dependencies && dependencies->size()==2 && dependencies->front().change==Engine::Editor::PrefabThreeWayChange::Conflict,"Restoration concealed changed parent conflict");
        CancelEditorPrefabFieldRevert();Require(unchanged() && RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error) && !ConfirmEditorPrefabEntityRestoration(error) && !HasPendingPrefabEntityRestoration() && unchanged(),"Restoration cancel/default conflict refusal changed state");
        Require(RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error),error);auto stale=incoming;stale["world"]["entities"][size_t(0)]["name"]="Changed after restoration preview";
        Require(Engine::Editor::SavePrefabDocument(source,stale,error,true) && !ConfirmEditorPrefabEntityRestoration(error,true) && Engine::Editor::SavePrefabDocument(source,incoming,error,true) && unchanged(),"Restoration approval bypassed source revision");
        Require(RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error) && SelectEditorEntity(1,false,false,error) && !ConfirmEditorPrefabEntityRestoration(error,true) && SelectEditorEntity(2,true,false,error) && unchanged(),"Restoration approval bypassed full-selection guard");
        for(const auto& path:std::vector<std::filesystem::path>{source,Engine::Assets::AssetRegistry::MetadataPath(source)}){
            Require(RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error),error);const HANDLE writer=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Require(writer!=INVALID_HANDLE_VALUE,"Restoration writer fixture failed");const bool accepted=ConfirmEditorPrefabEntityRestoration(error,true);CloseHandle(writer);Require(!accepted && unchanged(),"Restoration accepted source/metadata writer");
        }
        Access::ReplaceHistory(*this,Engine::Editor::EntityHistory(1,1));const auto tinyHistory=Access::History(*this);
        Require(!RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error) && !HasPendingPrefabEntityRestoration() && Access::World(*this)==restorationBefore_ && Access::History(*this).HasSameState(tinyHistory) && Access::Document(*this).HasSameState(document) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2},"Single-pass restoration history-budget refusal published state");Access::ReplaceHistory(*this,history);
        Require(unchanged() && RequestEditorPrefabEntityRestoration(renderedPrefabRoot_,prefabGuid_,row,error) && Access::StructuralPreviewMatchesCore(*this,error),"Single-pass restoration preview differs from core command");
    }
    void PrepareRenderedEntityRemoval(std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
        auto incoming=Engine::Editor::PrefabValue::parse(adoptionSource_);incoming["world"]["entities"][size_t(2)]["parent"]=2.0;
        Require(Engine::Editor::SavePrefabDocument(source,incoming,error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
        const auto& added=GetEditorPrefabThreeWay()->entries;const auto child=std::find_if(added.begin(),added.end(),[](const auto& row){return row.local==3 && row.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(child!=added.end(),"Removal setup addition missing");
        entityRemovalParent_=Access::HighWater(*this);entityRemovalChild_=entityRemovalParent_+1;
        Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,*child,error) && ConfirmEditorPrefabEntityAddition(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(entityRemovalChild_,true,false,error) && Access::RenameRemovalFixture(*this,entityRemovalChild_,error) && SaveSceneAs(originalScene_,error,true),error);
        auto& nodes=incoming["world"]["entities"].get<crude_json::array>();nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[](const auto& node){const auto id=node["id"].template get<crude_json::number>();return id==2 || id==3;}),nodes.end());
        Require(Engine::Editor::SavePrefabDocument(source,incoming,error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
        const auto& entries=GetEditorPrefabThreeWay()->entries;const auto found=std::find_if(entries.begin(),entries.end(),[](const auto& row){return row.local==2 && row.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(found!=entries.end(),"Removal parent row missing");const auto row=*found;
        entityRemovalBefore_=Access::World(*this);entityRemovalSource_=ReadAdoptionFixture(source);entityRemovalSaved_=ReadAdoptionFixture(originalScene_);const auto history=Access::History(*this);const auto document=Access::Document(*this);const auto high=Access::HighWater(*this);
        const auto unchanged=[&]{return Access::World(*this)==entityRemovalBefore_ && Access::HighWater(*this)==high && Access::History(*this).HasSameState(history) && Access::Document(*this).HasSameState(document) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,entityRemovalChild_} && ReadAdoptionFixture(source)==entityRemovalSource_ && ReadAdoptionFixture(originalScene_)==entityRemovalSaved_;};
        Require(!RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,"wrong-guid",row,error) && RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error) && !HasPendingPrefabFieldAdoption() && !ConfirmEditorPrefabFieldRevert(error,true) && HasPendingPrefabEntityRemoval() && !ConfirmEditorPrefabEntityAddition(error) && !RequestEditorPrefabApply(renderedPrefabRoot_,error),"Removal GUID/type/mutual guard failed");
        const auto* closure=GetPendingPrefabEntityRemovals();Require(closure && closure->size()==2 && (*closure)[1].change==Engine::Editor::PrefabThreeWayChange::Conflict,"Removal review concealed descendant conflict");
        CancelEditorPrefabFieldRevert();Require(unchanged() && !HasPendingPrefabEntityRemoval() && RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error) && !ConfirmEditorPrefabEntityRemoval(error) && !HasPendingPrefabEntityRemoval() && unchanged(),"Removal cancel/default conflict refusal changed authoring");
        Require(RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error),error);auto stale=incoming;stale["world"]["entities"][size_t(0)]["name"]="Changed after removal preview";
        Require(Engine::Editor::SavePrefabDocument(source,stale,error,true) && !ConfirmEditorPrefabEntityRemoval(error,true) && Engine::Editor::SavePrefabDocument(source,incoming,error,true) && unchanged(),"Removal approval bypassed native source revision");
        Require(RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error) && SelectEditorEntity(1,false,false,error) && !ConfirmEditorPrefabEntityRemoval(error,true) && SelectEditorEntity(entityRemovalChild_,true,false,error) && unchanged(),"Removal approval bypassed selection guard");
        Require(RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error) && RequestNewScene(error) && !ConfirmEditorPrefabEntityRemoval(error,true) && ConfirmDocumentAction(DocumentDecision::Cancel,error) && unchanged(),"Removal approval bypassed pending document guard");
        Require(RequestEditorPrefabEntityRemoval(renderedPrefabRoot_,prefabGuid_,row,error) && Access::StructuralPreviewMatchesCore(*this,error),"Single-pass removal preview differs from core command");
    }
    void PrepareRenderedAncestorAddition(std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
        auto incoming=Engine::Editor::PrefabValue::parse(adoptionSource_);incoming["world"]["entities"][size_t(2)]["parent"]=2.0;
        Require(Engine::Editor::SavePrefabDocument(source,incoming,error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
        const auto& entries=GetEditorPrefabThreeWay()->entries;const auto found=std::find_if(entries.begin(),entries.end(),[](const auto& row){return row.local==3 && row.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(found!=entries.end(),"Actual ancestor addition row missing");const auto row=*found;
        entityAncestorBefore_=Access::World(*this);entityAncestorSource_=ReadAdoptionFixture(source);entityAncestorSaved_=ReadAdoptionFixture(originalScene_);
        const auto history=Access::History(*this);const auto document=Access::Document(*this);const auto high=Access::HighWater(*this);
        const auto unchanged=[&]{return Access::World(*this)==entityAncestorBefore_ && Access::HighWater(*this)==high && Access::History(*this).HasSameState(history) && Access::Document(*this).HasSameState(document) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2} && ReadAdoptionFixture(source)==entityAncestorSource_ && ReadAdoptionFixture(originalScene_)==entityAncestorSaved_ && !HasPendingPrefabEntityAddition();};
        Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,row,error),error);auto stale=incoming;stale["world"]["entities"][size_t(0)]["name"]="Changed after addition preview";
        Require(Engine::Editor::SavePrefabDocument(source,stale,error,true) && !ConfirmEditorPrefabEntityAddition(error) && !HasPendingPrefabEntityAddition() && ReadAdoptionFixture(source)!=entityAncestorSource_ && Engine::Editor::SavePrefabDocument(source,incoming,error,true) && unchanged(),"Addition confirmation ignored source revision or changed authoring on rejection");
        Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,row,error) && SelectEditorEntity(renderedPrefabRoot_,false,false,error) && !ConfirmEditorPrefabEntityAddition(error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && unchanged(),"Addition confirmation ignored changed full selection");
        Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,row,error) && RequestNewScene(error) && !ConfirmEditorPrefabEntityAddition(error) && ConfirmDocumentAction(DocumentDecision::Cancel,error) && unchanged(),"Addition confirmation ignored pending document action");
        for(const auto& lockedPath:std::vector<std::filesystem::path>{source,Engine::Assets::AssetRegistry::MetadataPath(source)}){
            Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,row,error),error);
            const HANDLE writer=CreateFileW(lockedPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Require(writer!=INVALID_HANDLE_VALUE,"Addition writer fixture open failed");const bool accepted=ConfirmEditorPrefabEntityAddition(error);CloseHandle(writer);
            Require(!accepted && unchanged(),"Addition confirmation accepted source/metadata writer or changed authoring");
        }
        entityAncestorParent_=high;entityAncestorChild_=high+1;
        Require(RequestEditorPrefabEntityAddition(renderedPrefabRoot_,prefabGuid_,row,error),error);
    }
    void PrepareRenderedAdoption(std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;
        const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
        renderedConflictSource_=ReadAdoptionFixture(source);adoptionWorld_=Access::World(*this);adoptionSavedScene_=ReadAdoptionFixture(originalScene_);
        auto changed=Engine::Editor::PrefabValue::parse(renderedConflictSource_);changed["world"]["entities"][size_t(0)]["transform"]["rotation"]=crude_json::array{0.0,1.0,0.0};
        for(int index=0;index<70;++index){auto child=changed["world"]["entities"][size_t(0)];child["id"]=double(index+2);child["parent"]=1.0;child["name"]="Adoption source child "+std::to_string(index);changed["world"]["entities"].push_back(child);}
        Require(Engine::Editor::SavePrefabDocument(source,changed,error,true) && InspectEditorPrefabThreeWay(renderedPrefabRoot_,error),error);
        adoptionSource_=ReadAdoptionFixture(source);const auto& report=*GetEditorPrefabThreeWay();
        const auto found=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& row){return row.path=="/transform/rotation";});Require(found!=report.entries.end(),"Source-only adoption row missing");const auto entry=*found;
        const auto presence=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& row){return row.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence;});Require(presence!=report.entries.end(),"Presence refusal row missing");
        const auto history=Access::History(*this);const auto selection=GetEditorSelection();
        Require(!RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,"stale-guid",entry,error) && !RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,*presence,error) && !HasPendingPrefabFieldAdoption(),"Adoption accepted stale GUID or Entity presence");
        auto invented=entry;invented.source="invented";Require(!RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,invented,error),"Adoption accepted stale comparison values");
        Require(RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,entry,error) && HasPendingPrefabFieldAdoption() && !RequestEditorPrefabApply(renderedPrefabRoot_,error),"Adoption preview omitted mutual exclusion");
        CancelEditorPrefabFieldRevert();Require(Access::World(*this)==adoptionWorld_ && Access::History(*this).HasSameState(history) && GetEditorSelection()==selection && !IsSceneDirty(),"Adoption preview/cancel mutated live state");
        Require(RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,entry,error) && SelectEditorEntity(renderedPrefabRoot_,false,false,error) && !ConfirmEditorPrefabFieldAdoption(error) && !HasPendingPrefabFieldAdoption(),"Adoption ignored changed selection");
        Require(SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,entry,error) && RequestNewScene(error) && !ConfirmEditorPrefabFieldAdoption(error) && ConfirmDocumentAction(DocumentDecision::Cancel,error),"Adoption ignored pending document action");
        Require(RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,entry,error),error);auto stale=changed;stale["world"]["entities"][size_t(0)]["name"]="Changed after preview";
        Require(Engine::Editor::SavePrefabDocument(source,stale,error,true) && !ConfirmEditorPrefabFieldAdoption(error,true) && Engine::Editor::SavePrefabDocument(source,changed,error,true),"Adoption approval bypassed source revision guard");
        Require(Access::World(*this)==adoptionWorld_ && Access::History(*this).HasSameState(history) && GetEditorSelection()==selection && !IsSceneDirty() && ReadAdoptionFixture(source)==adoptionSource_ && ReadAdoptionFixture(originalScene_)==adoptionSavedScene_,"Refused adoption mutated authoring or files");
        Require(RequestEditorPrefabFieldAdoption(renderedPrefabRoot_,prefabGuid_,entry,error),error);
    }
    void FinishRenderedAdoption(std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";
        Require(ConfirmEditorPrefabFieldAdoption(error) && !HasPendingPrefabFieldAdoption() && !ConfirmEditorPrefabFieldAdoption(error) && IsSceneDirty(),"Adoption publication/single-use/dirty failed");const auto after=Access::World(*this);
        Require(after!=adoptionWorld_ && ReadAdoptionFixture(source)==adoptionSource_ && ReadAdoptionFixture(originalScene_)==adoptionSavedScene_ && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{1,2},"Adoption wrote source/saved Scene or lost selection");
        Require(ApplyEditorHistory(false,error) && Access::World(*this)==adoptionWorld_ && !IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==after && IsSceneDirty() && SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),"Adoption Undo/Redo/save baseline failed");
        Require(ApplyEditorHistory(false,error) && Access::World(*this)==adoptionWorld_ && IsSceneDirty() && ApplyEditorHistory(true,error) && !IsSceneDirty() && ApplyEditorHistory(false,error) && IsSceneDirty() && RestoreRenderedConflictSource(error) && SaveSceneAs(originalScene_,error,true) && !IsSceneDirty(),"Adoption saved-baseline Undo/Redo/fixture restore failed");
        std::cout<<"PASS: actual Game source-only field adoption, stale GUID/value/presence/selection/document/source refusal, single-use confirmation, source and saved Scene isolation, full selection/Undo/Redo/saved dirty baseline and rendered modal (not physical button QA)\n";
    }
    void PrepareRenderedFieldConflict(bool apply,std::string& error,bool whole=false){
        const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";std::ifstream input(source,std::ios::binary);Require(bool(input),"Rendered conflict fixture read failed");renderedConflictSource_=std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());input.close();
        Matrix4x4 from{};for(int axis=0;axis<4;++axis)from.m[axis][axis]=1;auto to=from;to.m[3][0]=1;
        Require(SelectEditorEntity(renderedPrefabRoot_,false,false,error) && TransformEditorSelection(from,to,0,error) && InspectEditorPrefabOverrides(renderedPrefabRoot_,error) && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error),error);
        auto changed=Engine::Editor::PrefabValue::parse(renderedConflictSource_);changed["world"]["entities"][size_t(0)]["transform"]["position"]=crude_json::array{9.0,0.0,0.0};
        Require(Engine::Editor::SavePrefabDocument(source,changed,error,true),error);const auto entry=GetEditorPrefabOverrides().entries.front();
        Require(whole?RequestEditorPrefabApply(renderedPrefabRoot_,error):apply?RequestEditorPrefabFieldApply(renderedPrefabRoot_,entry,error):RequestEditorPrefabFieldRevert(renderedPrefabRoot_,entry,error),error);
    }
    bool RestoreRenderedConflictSource(std::string& error){return Engine::Editor::SavePrefabDocument(fixture_/L"Assets/プレハブ/モデル.prefab",Engine::Editor::PrefabValue::parse(renderedConflictSource_),error,true);}
    void CheckFullThreeWay(Engine::Scene::EntityId root,const std::filesystem::path& source,std::string& error){
        using Access=Engine::Scene::GameApplicationChecksAccess;using C=Engine::Editor::PrefabThreeWayChange;
        const auto read=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Require(bool(input),"Comparison fixture read failed");return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());};
        const auto originalSource=read(source),savedScene=read(originalScene_),world=Access::World(*this);const auto selection=GetEditorSelection();const auto history=Access::History(*this);const bool dirty=IsSceneDirty();
        auto changed=Engine::Editor::PrefabValue::parse(originalSource);changed["world"]["entities"][size_t(0)]["transform"]["rotation"]=crude_json::array{0.0,1.0,0.0};
        for(int index=0;index<70;++index){auto child=changed["world"]["entities"][size_t(0)];child["id"]=double(index+2);child["parent"]=1.0;child["name"]="Source-only 日本語 child "+std::to_string(index);changed["world"]["entities"].push_back(child);}
        Require(Engine::Editor::SavePrefabDocument(source,changed,error,true),error);const auto changedBytes=read(source);
        Require(InspectEditorPrefabThreeWay(root,error) && GetEditorPrefabThreeWay(),error);const auto* retained=GetEditorPrefabThreeWay();
        bool conflict=false,sourceOnly=false,presence=false,instanceOnly=false;
        for(const auto& entry:retained->entries){if(entry.path=="/transform/position")conflict=entry.change==C::Conflict;if(entry.path=="/transform/rotation")sourceOnly=entry.change==C::SourceOnly;if(entry.local==2 && entry.kind==Engine::Editor::PrefabThreeWayKind::EntityPresence)presence=entry.change==C::SourceOnly && entry.scene==0 && !entry.baseline && !entry.instance && entry.source.has_value();if(entry.path=="/name")instanceOnly=entry.change==C::InstanceOnly;}
        Require(conflict && sourceOnly && presence && instanceOnly && Access::World(*this)==world && GetEditorSelection()==selection && Access::History(*this).HasSameState(history) && IsSceneDirty()==dirty && read(source)==changedBytes && read(originalScene_)==savedScene,"Full comparison lost classifications or mutated authoring/files");
        auto invalid=changed;invalid["format"]="invalid";{std::ofstream output(source,std::ios::binary);output<<invalid.dump();output.close();Require(!output.fail(),"Invalid comparison source fixture write failed");}
        const auto invalidBytes=read(source);Require(!InspectEditorPrefabThreeWay(root,error) && GetEditorPrefabThreeWay()==retained && read(source)==invalidBytes && Access::World(*this)==world && Access::History(*this).HasSameState(history),"Failed refresh replaced snapshot or modified invalid source/Scene");
        Require(Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(originalSource),error,true) && !InspectEditorPrefabThreeWay(1,error) && GetEditorPrefabThreeWay()==retained && read(source)==originalSource && read(originalScene_)==savedScene && GetEditorSelection()==selection && IsSceneDirty()==dirty,"Unlinked comparison lost prior snapshot/binding");
        std::cout<<"PASS: actual Game held-source full three-way snapshot, conflict/source-only/instance-only/source-Entity presence, source/saved Scene/history/full selection/dirty isolation and invalid/unlinked refresh retaining report\n";
    }
    void CheckFieldApply(Engine::Scene::EntityId root){
        using Access=Engine::Scene::GameApplicationChecksAccess;std::string error;
        const auto read=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Require(bool(input),"Field Apply fixture read failed");return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());};
        const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";const auto sourceBytes=read(source),sceneBytes=read(originalScene_),initialWorld=Access::World(*this);
        const auto selection=GetEditorSelection();Engine::Scene::EntityId other=0;
        Require(InstantiateEditorPrefab(prefabGuid_,2,other,error) && Access::RenameFieldFixture(*this,root,other,error),error);
        Matrix4x4 from{};for(int axis=0;axis<4;++axis)from.m[axis][axis]=1;auto to=from;to.m[3][0]=1;
        Require(SelectEditorEntity(root,false,false,error) && TransformEditorSelection(from,to,0,error) && InspectEditorPrefabOverrides(root,error),error);
        const auto found=std::find_if(GetEditorPrefabOverrides().entries.begin(),GetEditorPrefabOverrides().entries.end(),[](const auto& entry){return entry.path=="/transform/position";});
        Require(found!=GetEditorPrefabOverrides().entries.end(),"Field Apply position override missing");const auto entry=*found;
        Require(SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error),error);
        const auto before=Access::World(*this);
        Require(RequestEditorPrefabFieldApply(root,entry,error) && HasPendingPrefabApply() && !RequestEditorPrefabApply(root,error) && !RequestEditorPrefabFieldRevert(root,entry,error),"Field Apply preview omitted mutual exclusion");
        CancelEditorPrefabApply();Require(!HasPendingPrefabApply() && Access::World(*this)==before && GetEditorSelection()==selection && IsSceneDirty() && read(source)==sourceBytes && read(originalScene_)==sceneBytes,"Field Apply preview/cancel mutated data");
        Require(RequestEditorPrefabFieldApply(root,entry,error) && SelectEditorEntity(root,false,false,error) && !ConfirmEditorPrefabApply(error) && !HasPendingPrefabApply() && Access::World(*this)==before && read(source)==sourceBytes,"Field Apply ignored changed selection");
        Require(SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && RequestEditorPrefabFieldApply(root,entry,error) && RequestNewScene(error) && HasPendingDocumentAction() && !ConfirmEditorPrefabApply(error) && Access::World(*this)==before && ConfirmDocumentAction(DocumentDecision::Cancel,error),"Field Apply ignored pending document action");
        Require(RequestEditorPrefabFieldApply(root,entry,error),error);auto changed=Engine::Editor::PrefabValue::parse(sourceBytes);changed["world"]["entities"][size_t(0)]["name"]="External source revision";
        Require(Engine::Editor::SavePrefabDocument(source,changed,error,true) && !ConfirmEditorPrefabApply(error) && Access::World(*this)==before && Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(sourceBytes),error,true),"Field Apply ignored changed source revision");
        auto conflicting=Engine::Editor::PrefabValue::parse(sourceBytes);conflicting["world"]["entities"][size_t(0)]["transform"]["position"]=crude_json::array{9.0,0.0,0.0};
        Require(Engine::Editor::SavePrefabDocument(source,conflicting,error,true),error);const auto conflictingBytes=read(source);
        CheckFullThreeWay(root,source,error);
        Require(RequestEditorPrefabFieldApply(root,entry,error) && GetPendingPrefabFieldApplyChange()==Engine::Editor::PrefabThreeWayChange::Conflict && !ConfirmEditorPrefabApply(error) && !HasPendingPrefabApply() && Access::World(*this)==before && read(source)==conflictingBytes && GetEditorSelection()==selection,"Unapproved field conflict wrote data or omitted classification");
        Require(RequestEditorPrefabFieldApply(root,entry,error) && ConfirmEditorPrefabApply(error,true) && ApplyEditorHistory(false,error) && Access::World(*this)==before && read(source)==conflictingBytes && ApplyEditorHistory(true,error) && ApplyEditorHistory(false,error) && Access::World(*this)==before && read(source)==conflictingBytes,"Approved field Apply conflict/replay failed");
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && GetPendingPrefabFieldRevertChange()==Engine::Editor::PrefabThreeWayChange::Conflict && !ConfirmEditorPrefabFieldRevert(error) && !HasPendingPrefabFieldRevert() && Access::World(*this)==before && read(source)==conflictingBytes,"Unapproved field Revert conflict wrote data");
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && ConfirmEditorPrefabFieldRevert(error,true) && read(source)==conflictingBytes && ApplyEditorHistory(false,error) && Access::World(*this)==before && ApplyEditorHistory(true,error) && ApplyEditorHistory(false,error) && Access::World(*this)==before && read(source)==conflictingBytes,"Approved field Revert conflict/replay changed source");
        Require(RequestEditorPrefabFieldApply(root,entry,error) && Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(sourceBytes),error,true) && !ConfirmEditorPrefabApply(error,true) && Access::World(*this)==before,"Explicit approval bypassed current source revision guard");
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && Engine::Editor::SavePrefabDocument(source,conflicting,error,true) && !ConfirmEditorPrefabFieldRevert(error,true) && Access::World(*this)==before && Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(sourceBytes),error,true),"Explicit Revert approval bypassed current source revision guard");
        Require(RequestEditorPrefabFieldApply(root,entry,error) && GetPendingPrefabFieldApplyChange()==Engine::Editor::PrefabThreeWayChange::InstanceOnly && ConfirmEditorPrefabApply(error) && !HasPendingPrefabApply() && !ConfirmEditorPrefabApply(error) && GetEditorSelection()==selection && IsSceneDirty() && !HasPendingPrefabRecovery(),"Actual field Apply publication/selection/dirty/single use failed");
        const auto after=Access::World(*this),afterSource=read(source);auto expected=Engine::Editor::PrefabValue::parse(sourceBytes);
        expected["world"]["entities"][size_t(0)]["transform"]["position"]=Engine::Editor::PrefabValue::parse(*entry.instance);
        Require(Engine::Editor::PrefabValue::parse(afterSource).dump()==expected.dump() && read(originalScene_)==sceneBytes,"Field Apply wrote unselected source fields or saved Scene");
        const auto afterWorld=Engine::Editor::PrefabValue::parse(after);const auto find=[&](Engine::Scene::EntityId id)->const Engine::Editor::PrefabValue&{for(const auto& node:afterWorld["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(id))return node;throw std::runtime_error("Field fixture Entity absent");};
        Require(find(root)["name"].get<crude_json::string>()=="Elected unselected name" && find(other)["name"].get<crude_json::string>()=="Other instance-owned name" && find(root)["transform"]["position"].dump()==find(other)["transform"]["position"].dump(),"Field propagation lost inheritance or instance-owned names");
        Require(ApplyEditorHistory(false,error) && Access::World(*this)==before && read(source)==sourceBytes && IsSceneDirty() && ApplyEditorHistory(true,error) && Access::World(*this)==after && read(source)==afterSource && GetEditorSelection()==selection && ApplyEditorHistory(false,error),"Actual Native field Undo/Redo failed");
        Require(Engine::Editor::SavePrefabDocument(source,expected,error,true),error);const auto equalSource=read(source);
        Require(RequestEditorPrefabFieldApply(root,entry,error) && GetPendingPrefabFieldApplyChange()==Engine::Editor::PrefabThreeWayChange::Converged && ConfirmEditorPrefabApply(error) && Access::World(*this)==after && read(source)==equalSource && ApplyEditorHistory(false,error) && Access::World(*this)==before && read(source)==equalSource && ApplyEditorHistory(true,error) && Access::World(*this)==after && read(source)==equalSource && ApplyEditorHistory(false,error),"Source-equal field Apply refresh/replay changed source");
        Require(Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(sourceBytes),error,true) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && ApplyEditorHistory(false,error) && Access::World(*this)==initialWorld && !IsSceneDirty() && SelectEditorEntity(1,false,false,error) && SelectEditorEntity(2,true,false,error) && GetEditorSelection()==selection && read(source)==sourceBytes && read(originalScene_)==sceneBytes,error);
        std::cout<<"PASS: actual Editor field Apply preview/cancel/mutual exclusion, stale source/selection/pending refusal, selected-only native source and two-instance propagation, unselected names, full selection/dirty/source Undo/Redo and source-equal Scene-only refresh\n";
        std::cout<<"PASS: actual held-source field three-way classification, unapproved Apply/Revert conflict refusal, explicit instance/source choice with native/Scene Undo/Redo and approved stale-revision refusal\n";
    }
    void CheckFieldRevert(Engine::Scene::EntityId root,const Engine::Editor::PrefabOverrideEntry& requested){
        using Access=Engine::Scene::GameApplicationChecksAccess;const auto entry=requested;std::string error;
        const auto readBytes=[](const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Require(bool(input),"Field Revert fixture read failed");return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());};
        const auto source=fixture_/L"Assets/プレハブ/モデル.prefab";const auto sourceBytes=readBytes(source),sceneBytes=readBytes(originalScene_);
        const auto world=Access::World(*this);const auto selection=GetEditorSelection();
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && HasPendingPrefabFieldRevert() && !RequestEditorPrefabFieldRevert(root,entry,error) && !RequestEditorPrefabApply(root,error),"Field preview omitted pending flag/mutual exclusion");
        CancelEditorPrefabFieldRevert();Require(!HasPendingPrefabFieldRevert() && Access::World(*this)==world && GetEditorSelection()==selection && IsSceneDirty() && readBytes(source)==sourceBytes && readBytes(originalScene_)==sceneBytes,"Preview/cancel changed source/Scene/selection/dirty");
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && SelectEditorEntity(1,false,false,error) && !ConfirmEditorPrefabFieldRevert(error) && !HasPendingPrefabFieldRevert() && Access::World(*this)==world && SelectEditorEntity(root,false,false,error),"Field confirmation ignored changed selection");
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && RequestNewScene(error) && HasPendingDocumentAction() && !ConfirmEditorPrefabFieldRevert(error) && Access::World(*this)==world && ConfirmDocumentAction(DocumentDecision::Cancel,error),"Field confirmation ignored pending document action");
        Require(RequestEditorPrefabFieldRevert(root,entry,error),error);
        auto changed=Engine::Editor::PrefabValue::parse(sourceBytes);changed["world"]["entities"][size_t(0)]["name"]="Changed while awaiting field confirmation";
        Require(Engine::Editor::SavePrefabDocument(source,changed,error,true) && !ConfirmEditorPrefabFieldRevert(error) && Access::World(*this)==world && IsSceneDirty(),"Field confirmation ignored changed native source revision");
        Require(Engine::Editor::SavePrefabDocument(source,Engine::Editor::PrefabValue::parse(sourceBytes),error,true) && readBytes(source)==sourceBytes,error);
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && SaveSceneAs(originalScene_,error,true) && !ConfirmEditorPrefabFieldRevert(error) && !IsSceneDirty() && Access::World(*this)==world,"Field confirmation ignored saved document baseline change");
        Require(ApplyEditorHistory(false,error) && SaveSceneAs(originalScene_,error,true) && ApplyEditorHistory(true,error) && IsSceneDirty() && Access::World(*this)==world && readBytes(originalScene_)==sceneBytes,error);
        Require(RequestEditorPrefabFieldRevert(root,entry,error) && ConfirmEditorPrefabFieldRevert(error) && !HasPendingPrefabFieldRevert() && !IsSceneDirty() && GetEditorSelection()==selection && readBytes(source)==sourceBytes && readBytes(originalScene_)==sceneBytes,"Field confirmation lost source/selection/saved baseline");
        Require(ApplyEditorHistory(false,error) && IsSceneDirty() && Access::World(*this)==world && ApplyEditorHistory(true,error) && !IsSceneDirty() && ApplyEditorHistory(false,error) && IsSceneDirty() && Access::World(*this)==world,"Actual field Undo/Redo lost authored data/dirty state");
        std::cout<<"PASS: actual Editor field Revert preview/cancel, current source identity and selection/document-baseline/pending refusal, source/saved Scene byte preservation, dirty Undo/Redo and single-use confirmation\n";
    }
    void CheckReplayPending(const std::filesystem::path& source,const Engine::Editor::PrefabValue& original){
        using Access=Engine::Scene::GameApplicationChecksAccess;
        const auto read=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Require(bool(in),"Replay pending fixture read failed");return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());};
        const auto write=[](const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Replay pending fixture write failed");};
        for(const bool collision:{false,true}){
            std::string error;Engine::Base::ProjectContext project;Engine::Assets::AssetRegistry registry;Engine::Editor::ProjectPrefabRevision revision;
            Require(project.Open(manifest_,error) && registry.Scan(fixture_/"Assets",error) && Engine::Editor::ReadProjectPrefabRevision(project,registry,prefabGuid_,revision,error),error);
            auto next=revision.definition;next["world"]["entities"][size_t(0)]["name"]=collision?"Collision replay fixture":"Unknown replay fixture";
            {Engine::Editor::PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error) && publication.Publish(error),error);}
            Engine::Editor::PrefabSourceRecoveryReport receipt;std::filesystem::path archive;
            Require(Engine::Editor::PrefabSourceRecovery::Inspect(project,prefabGuid_,receipt,error) && Engine::Editor::PrefabSourceAcknowledgement::ArchiveResolved(project,prefabGuid_,receipt,archive,error),error);receipt.directory=archive;
            const auto pending=fixture_/"Assets/.kubo/prefab-transactions"/prefabGuid_;
            auto effect=std::make_shared<Engine::Editor::PrefabSourceHistoryEffect>(project,receipt,[&]{
                write(pending/"unknown-owner","preserve unknown replay data");
                if(collision){write(source,"foreign source owner");throw std::runtime_error("Injected owned replay collision");}
            });
            const auto sceneBytes=read(originalScene_);const auto world=Access::World(*this);const auto selected=GetEditorSelection();
            Require(!HasPendingPrefabRecovery() && !IsSceneDirty() && Access::RecordSourceOnly(*this,effect,error),error);
            const bool applied=ApplyEditorHistory(false,error);
            Require(applied!=collision && HasPendingPrefabRecovery() && Access::Editing(*this) && Access::World(*this)==world && GetEditorSelection()==selected && !IsSceneDirty() && read(originalScene_)==sceneBytes && read(pending/"unknown-owner")=="preserve unknown replay data","Native replay did not publish immediate pending guard/preserve Scene/selection/unknown data");
            Engine::Scene::EntityId created=999;
            Require(!CreateEditorEntity("Blocked after replay",0,created,error) && created==999 && !ApplyEditorHistory(true,error) && !PlayEditorScene(error) &&
                !SaveSceneAs(originalScene_,error,true) && !SaveExportSettings() && !RequestNewScene(error) && Access::Editing(*this) && Access::World(*this)==world && GetEditorSelection()==selected && !IsSceneDirty() && read(originalScene_)==sceneBytes,"Same-frame post-replay commands bypassed pending guard");
            if(collision){
                Require(read(source)=="foreign source owner" && std::filesystem::is_regular_file(pending/"history.json"),"Replay collision lost foreign source/history intent");
                // Manual repair only on the runner-owned fixture; preserve foreign data.
                const auto retained=fixture_/"retained-replay-foreign-source";std::filesystem::rename(source,retained);
                Require(InspectEditorPrefabRecovery(prefabGuid_,error) && RestoreEditorPrefabRecovery(error) && CompleteEditorPrefabRecovery(error) && !HasPendingPrefabRecovery() && read(retained)=="foreign source owner",error);
            }else{
                Require(read(source)==revision.bytes && !std::filesystem::exists(pending/"history.json"),"Successful replay did not retain unknown-only pending state");
                // Unknown-only data has no valid marker; manual archival is not
                // presented as an automatic recovery feature.
                std::filesystem::rename(pending,fixture_/"retained-replay-unknown");Access::ForgetHistory(*this);
                Require(RefreshPrefabRecovery(error) && !HasPendingPrefabRecovery(),error);
            }
            Require(Access::World(*this)==world && GetEditorSelection()==selected && !IsSceneDirty() && read(originalScene_)==sceneBytes,"Replay completion rewrote preserved Scene/selection/baseline");
            Require(Engine::Editor::SavePrefabDocument(source,original,error,true),error);
        }
    }
    void WriteAlternate(const std::string& bytes) {std::ofstream output(alternateScene_,std::ios::binary);output<<bytes;output.close();Require(!output.fail(),"Alternate Scene write failed");}
    void CheckOwnedTexture(const std::string& relative) {
        auto* textures=Engine::Base::TextureManager::GetInstance();
        const auto expected=Engine::Base::PathToUtf8(originalCwd_/"Resources/textures/common/uvChecker.png");
        const auto competitor=Engine::Base::PathToUtf8(originalCwd_/"Resources/textures/common/white.png");
        textures->LoadTextures({relative,expected,competitor});
        const auto& metadata=textures->GetMetaData(relative);
        const auto& original=textures->GetMetaData(expected);
        const auto& other=textures->GetMetaData(competitor);
        Require((original.width!=other.width || original.height!=other.height) && metadata.width==original.width && metadata.height==original.height,"GPU texture substituted cwd competitor dimensions");
        const auto absolute=Engine::Base::PathToUtf8(fixture_/Engine::Base::PathFromUtf8(relative));
        textures->LoadTexture(absolute);
        Require(textures->GetTextureIndexByFilePath(relative)==textures->GetTextureIndexByFilePath(absolute),"Owned absolute/relative GPU texture aliases diverged");
    }
    std::filesystem::path fixture_,originalCwd_;
    std::filesystem::path manifest_;std::string manifestBytes_;
    std::filesystem::path originalScene_,alternateScene_;std::string alternateBytes_,prefabGuid_;
    std::filesystem::path newSavedScene_;Engine::Scene::EntityId activeModel_=1;
    Engine::Scene::EntityId duplicatedModel_=0,duplicatedCamera_=0;
    Engine::Scene::EntityId renderedPrefabRoot_=0;bool prefabRenderExpected_=false;
    bool deletedDuplicates_=false,sawApplyModal_=false,sawFieldModal_=false,sawFieldApplyModal_=false,sawConflictRevertModal_=false,sawAdoptionModal_=false,sawRootRevertModal_=false,sawWholeConflictModal_=false,sawEntityAdditionModal_=false;
    std::string entityAdditionWorld_,entityAdditionAfterWorld_;
    bool sawEntityAncestorModal_=false,sawEntityAncestorModels_=false;
    Engine::Scene::EntityId entityAncestorParent_=0,entityAncestorChild_=0;
    std::string entityAncestorBefore_,entityAncestorAfter_,entityAncestorSource_,entityAncestorSaved_;
    bool sawEntityRemovalModal_=false,sawRemovedModelEntries_=false;
    Engine::Scene::EntityId entityRemovalParent_=0,entityRemovalChild_=0;
    std::string entityRemovalBefore_,entityRemovalAfter_,entityRemovalSource_,entityRemovalSaved_;
    bool sawRestorationModal_=false,sawRestoredModels_=false;
    std::string restorationBefore_,restorationAfter_,restorationSource_,restorationSaved_;
    std::string adoptionWorld_,adoptionSource_,adoptionSavedScene_;
    std::string renderedConflictSource_;
    std::string legacyLayout_;ImGuiID dockId_=0;
    int frames_=0,draws_=0;
    std::atomic<bool> pendingMcpSent_{false};std::future<Engine::Scene::SceneValue> pendingMcp_;
    bool sawViews_=false, sawResize_=false, sawOpenedScene_=false,sawNewScene_=false,sawSaveAs_=false;
};

// A real child process holds ExportJob running; no mock busy flag or sleep-only
// inference. The script is test-owned and bounded independently of this runner.
class McpExportGuardChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    using Value=Engine::Scene::SceneValue;
public:
    ~McpExportGuardChecks() override {
        Access::StopGuardExport(*this);
        if(pending_.valid()) {try {pending_.get();}catch(...) {}}
        if(passed_){std::error_code error;std::filesystem::remove_all(fixture_,error);}
        else if(!fixture_.empty()) std::cerr<<"Retained MCP export fixture: "<<fixture_<<'\n';
    }
    void Initialize() override {
        fixture_=std::filesystem::temp_directory_path()/(L"kubo-mcp-export-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(fixture_/"Assets");
        std::string error;Engine::Scene::EntityWorld world;world.CreateEntity("Export guard");
        Require(world.Save(fixture_/"guard.scene",error),error.c_str());
        {std::ofstream out(fixture_/"guard.kubo",std::ios::binary);out<<R"({"name":"MCP Export Guard","startupScene":"guard.scene"})";Require(bool(out),"Guard manifest write failed");}
        {std::ofstream out(fixture_/"guard.ps1",std::ios::binary);out<<R"(param($ProjectFile,$OutputDirectory,$Configuration,$RuntimeExecutable)
$ErrorActionPreference='Stop'
$guardRoot=Split-Path -Parent $ProjectFile
[IO.File]::WriteAllText((Join-Path $guardRoot 'export.ready'),'ready')
$guardDeadline=[DateTime]::UtcNow.AddSeconds(15)
while([DateTime]::UtcNow -lt $guardDeadline) { Start-Sleep -Milliseconds 20 }
exit 9
)";Require(bool(out),"Guard script write failed");}
        SetProjectRoot(fixture_);SetProjectFile(fixture_/"guard.kubo");SetProjectAssetRoot(fixture_/"Assets");
        SetEditorMode(true);SetMcpAtStartup(true);Require(ConfigureStartupScene(fixture_/"guard.scene",error),error.c_str());Game::Initialize();
        Require(Access::StartGuardExport(*this,fixture_,error),error.c_str());
        const auto deadline=GetTickCount64()+5000;
        while(!std::filesystem::exists(fixture_/"export.ready") && GetTickCount64()<deadline)Sleep(1);
        Require(std::filesystem::exists(fixture_/"export.ready") && Access::ExportRunning(*this),"Actual export child did not enter bounded running interval");
        before_=Access::World(*this);highWater_=Access::HighWater(*this);selection_=GetEditorSelection();history_=Access::History(*this);dirty_=IsSceneDirty();saved_=ReadSaved();
        Submit();
    }
    bool IsEndRequest() const override {return done_ || Game::IsEndRequest();}
    void Update() override {
        Require(GetTickCount64()<requestDeadline_,"MCP export guard response timed out");
        Game::Update();
        if(pending_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
        const auto reply=pending_.get();
        if(stage_<8){
            Require(Access::ExportRunning(*this),"Export ended before busy guard assertion");
            Require(stage_==7?!reply.contains("error"):reply.contains("error"),"Busy export mutation/save admitted or read refused");
            if(stage_<7)Require(reply["error"]["code"].get<double>()==-32000.0,"Busy request failed outside authoring lock");
            Require(Access::World(*this)==before_ && Access::HighWater(*this)==highWater_ && GetEditorSelection()==selection_ && Access::History(*this).HasSameState(history_) && IsSceneDirty()==dirty_,"Busy export request changed authoring state");
            Require(ReadSaved()==saved_,"Busy export request wrote saved Scene bytes");
            if(stage_==7){Require(Access::StopGuardExport(*this) && !Access::ExportRunning(*this),"Owned export child failed to cancel/terminate");}
        }else{
            Require(!reply.contains("error") && Access::World(*this)!=before_ && IsSceneDirty(),"Idle MCP edit failed after export termination");
            std::string error;Require(ApplyEditorHistory(false,error),error.c_str());
            Require(Access::World(*this)==before_ && GetEditorSelection()==selection_ && IsSceneDirty()==dirty_,"Post-export MCP Undo failed to restore baseline");done_=true;return;
        }
        ++stage_;Submit();
    }
    void Verify(){Require(done_,"Export guard runner stopped early");passed_=true;}
private:
    std::string ReadSaved() const {std::ifstream in(fixture_/"guard.scene",std::ios::binary);Require(bool(in),"Guard saved Scene read failed");return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
    void Submit(){
        static const char* requests[]={
            R"({"id":1,"method":"scene.set_component","params":{"entityId":1,"component":"camera","patch":{"enabled":true}}})",
            R"({"id":2,"method":"scene.set_component","params":{"entityId":1,"component":"collider","patch":{"enabled":true}}})",
            R"({"id":3,"method":"scene.set_component","params":{"entityId":1,"component":"rigidBody","patch":{"enabled":true}}})",
            R"({"id":4,"method":"scene.set_component","params":{"entityId":1,"component":"model","patch":{"asset":"","guid":null}}})",
            R"({"id":5,"method":"scene.set_component","params":{"entityId":1,"component":"script","patch":{"enabled":false}}})",
            R"({"id":6,"method":"scene.set_component","params":{"entityId":1,"component":"audio","patch":{"volume":0}}})",
            R"({"id":7,"method":"scene.save","params":{}})",
            R"({"id":8,"method":"scene.list_entities","params":{}})",
            R"({"id":9,"method":"scene.set_component","params":{"entityId":1,"component":"camera","patch":{"enabled":true}}})"};
        const auto request=Value::parse(requests[stage_]);requestDeadline_=GetTickCount64()+5000;
        pending_=std::async(std::launch::async,[root=fixture_,request]{return Engine::Services::EditorMcpSession::Call(root,request);});
    }
    std::filesystem::path fixture_;std::future<Value> pending_;int stage_=0;bool done_=false,passed_=false,dirty_=false;
    ULONGLONG requestDeadline_=0;std::string before_,saved_;Engine::Scene::EntityId highWater_=0;
    std::vector<Engine::Scene::EntityId> selection_;Engine::Editor::EntityHistory history_;
};

class GitEditorIndexChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
public:
    ~GitEditorIndexChecks() override {if(passed_){std::error_code cleanup;std::filesystem::remove_all(fixture_,cleanup);}else if(!fixture_.empty())std::cerr<<"Retained Git Editor fixture: "<<fixture_<<'\n';}
    void Initialize() override {
        fixture_=std::filesystem::temp_directory_path()/(L"kubo-git-editor-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));project_=fixture_/"Project";std::filesystem::create_directories(project_/"Assets");
        {std::ofstream out(project_/"entry.kubo");out<<R"({"name":"Git Editor","startupScene":"entry.scene"})";Require(bool(out),"Git manifest write failed");}
        Engine::Scene::EntityWorld world;world.CreateEntity("Git probe");std::string error;Require(world.Save(project_/"entry.scene",error),error.c_str());WriteChange("baseline");
        KuboGitFixture::Run(fixture_,{L"init",L"-b",L"main"});KuboGitFixture::Run(fixture_,{L"config",L"core.attributesFile",L"NUL"});KuboGitFixture::Run(fixture_,{L"config",L"user.name",L"Kubo test"});KuboGitFixture::Run(fixture_,{L"config",L"user.email",L"test@example.invalid"});KuboGitFixture::Run(fixture_,{L"add",L"--",L"."});KuboGitFixture::Run(fixture_,{L"commit",L"-m",L"isolated Editor fixture"});
        KuboGitFixture::Run(fixture_,{L"config",L"core.hooksPath",L"NUL"});KuboGitFixture::Run(fixture_,{L"config",L"commit.gpgsign",L"false"});
        SetProjectRoot(project_);SetProjectFile(project_/"entry.kubo");SetProjectAssetRoot(project_/"Assets");SetEditorMode(true);Require(ConfigureStartupScene(project_/"entry.scene",error),error.c_str());Game::Initialize();original_=Access::World(*this);selection_=GetEditorSelection();WriteChange("modified");started_=GetTickCount64();
    }
    bool IsEndRequest() const override {return step_>=10 || GetTickCount64()-started_>25000 || Game::IsEndRequest();}
    void Update() override {
        Game::Update();std::string error;
        if(Access::GitIndexBusy(*this) || Access::GitDiffBusy(*this) || Access::GitHistoryBusy(*this))return;
        if(step_==0){
            auto status=Engine::Services::GitWorkspace::ReadStatus(project_);auto selected=Change(status);
            Access::GitDirty(*this,true);Require(!StartGitIndexEdit(status,selected,true,error) && !Access::GitIndexBusy(*this),"Dirty Scene allowed Git index job");Access::GitDirty(*this,false);
            Require(PlayEditorScene(error),error.c_str());Require(!StartGitIndexEdit(status,selected,true,error),"Playing Editor allowed Git index job");Require(Access::Stop(*this,error),error.c_str());
            Require(StartGitIndexEdit(status,selected,true,error),error.c_str());Require(!StartGitIndexEdit(status,selected,true,error),"Concurrent index job accepted");step_=1;
        }else if(step_==1){
            Require(Access::GitIndexMessage(*this).find("staged;")!=std::string::npos,"Actual Editor stage result missing");auto status=Engine::Services::GitWorkspace::ReadStatus(project_);auto selected=Change(status);Require(selected.index=='M' && selected.worktree==' ',"Actual Editor did not stage disk content");Require(StartGitIndexEdit(status,selected,false,error),error.c_str());step_=2;
        }else if(step_==2){
            Require(Access::GitIndexMessage(*this).find("unstaged;")!=std::string::npos,"Actual Editor unstage result missing");auto status=Engine::Services::GitWorkspace::ReadStatus(project_);auto selected=Change(status);Require(selected.index==' ' && selected.worktree=='M',"Actual Editor did not unstage entry");
            KuboGitFixture::Run(fixture_,{L"add",L"--",L"Project/change.txt"});index_=ReadIndex();Require(StartGitIndexEdit(status,selected,true,error),error.c_str());step_=3;
        }else if(step_==3){
            Require(Access::GitIndexMessage(*this).find("changed")!=std::string::npos && ReadIndex()==index_,"Actual Editor stale job changed index");
            const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);head_=status.head;
            Access::GitDirty(*this,true);Require(!StartGitCommit(status,"dirty",error),"Dirty Scene allowed commit");Access::GitDirty(*this,false);
            Require(PlayEditorScene(error),error.c_str());Require(!StartGitCommit(status,"playing",error),"Playing Editor allowed commit");Require(Access::Stop(*this,error),error.c_str());
            WriteChange("newer unstaged");Require(StartGitCommit(status,"Editor staged commit 日本語",error),error.c_str());Require(!StartGitCommit(status,"duplicate",error),"Concurrent commit accepted");step_=4;
        }else if(step_==4){
            const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);
            Require(Access::GitIndexMessage(*this).find("committed;")!=std::string::npos && status.head!=head_ && status.indexRevision.empty(),"Actual Editor commit did not complete");
            const auto selected=Change(status);Require(selected.index==' ' && selected.worktree=='M',"Editor commit implicitly added newer disk bytes");head_=status.head;index_=ReadIndex();
            Access::GitDirty(*this,true);Require(StartGitDiff(status,selected,false,error),error.c_str());Access::GitDirty(*this,false);
            Require(!StartGitDiff(status,selected,false,error) && !StartGitCommit(status,"while diff",error) && !StartGitIndexEdit(status,selected,true,error),"Diff job admitted another Git request");step_=5;
        }else if(step_==5){
            Require(Access::GitDiffError(*this).empty() && Access::GitDiffText(*this).find("-modified")!=std::string::npos && Access::GitDiffText(*this).find("+newer unstaged")!=std::string::npos && ReadIndex()==index_ && Engine::Services::GitWorkspace::ReadStatus(project_).head==head_,"Actual Editor unstaged diff/read-only isolation failed");
            KuboGitFixture::Run(fixture_,{L"add",L"--",L"Project/change.txt"});const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);index_=ReadIndex();Require(StartGitDiff(status,Change(status),true,error),error.c_str());step_=6;
        }else if(step_==6){
            Require(Access::GitDiffError(*this).empty() && Access::GitDiffText(*this).find("+newer unstaged")!=std::string::npos && ReadIndex()==index_,"Actual Editor staged diff changed index");
            const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);KuboGitFixture::Run(fixture_,{L"restore",L"--staged",L"--",L"Project/change.txt"});index_=ReadIndex();Require(StartGitDiff(status,Change(status),true,error),error.c_str());step_=7;
        }else if(step_==7){
            Require(Access::GitDiffError(*this).find("changed")!=std::string::npos && Access::GitDiffText(*this).empty() && ReadIndex()==index_,"Actual Editor stale diff exposed output/changed index");
            const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);head_=status.head;
            Access::GitDirty(*this,true);Require(StartGitHistory(status,0,error),error.c_str());Access::GitDirty(*this,false);
            Require(!StartGitHistory(status,0,error) && !StartGitDiff(status,Change(status),false,error) && !StartGitCommit(status,"while history",error) && !StartGitIndexEdit(status,Change(status),true,error),"History job admitted another Git request");step_=8;
        }else if(step_==8){
            const auto& history=Access::GitHistory(*this);Require(Access::GitHistoryError(*this).empty() && history.head==head_ && history.entries.size()==2 && history.entries[0].subject=="Editor staged commit 日本語" && ReadIndex()==index_,"Actual Editor history fields/read-only isolation failed");
            const auto status=Engine::Services::GitWorkspace::ReadStatus(project_);KuboGitFixture::Run(fixture_,{L"commit",L"--allow-empty",L"-m",L"owned external head change"});index_=ReadIndex();Require(StartGitHistory(status,0,error),error.c_str());step_=9;
        }else if(step_==9){Require(Access::GitHistoryError(*this).find("changed")!=std::string::npos && Access::GitHistory(*this).entries.empty() && ReadIndex()==index_,"Actual Editor stale history exposed result/changed index");step_=10;
        }
        Require(Access::World(*this)==original_ && GetEditorSelection()==selection_ && !IsSceneDirty(),"Git index command changed authored World/selection/dirty");
        std::ifstream input(project_/"change.txt",std::ios::binary);Require(std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>())==(step_>=4 ? "newer unstaged" : "modified"),"Git command rewrote working file");
    }
    void Verify(){Require(step_==10,"Git Editor jobs did not complete");passed_=true;}
private:
    static Engine::Services::GitChange Change(const Engine::Services::GitStatus& status){Require(status.repository && status.error.empty(),status.error.c_str());for(const auto& change:status.changes)if(change.path=="Project/change.txt")return change;throw std::runtime_error("Git Editor probe absent");}
    void WriteChange(const char* text){std::ofstream out(project_/"change.txt",std::ios::binary);out<<text;out.close();Require(!out.fail(),"Git probe write failed");}
    std::string ReadIndex(){std::ifstream input(fixture_/".git/index",std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
    std::filesystem::path fixture_,project_;std::string original_,index_,head_;std::vector<Engine::Scene::EntityId> selection_;ULONGLONG started_=0;int step_=0;bool passed_=false;
};

class PrefabEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    RuntimePrefabHostFixture fixture_;int frames_=0,draws_=0;bool stopped_=false,finished_=false,retired_=false;
public:
    void Initialize() override{fixture_.Prepare("PrefabEditor");SetProjectRoot(fixture_.Root());SetProjectFile(fixture_.Root()/"entry.kubo");SetProjectAssetRoot(fixture_.Root()/"Assets");SetEditorMode(true);std::string error;Require(ConfigureStartupScene(fixture_.Root()/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());Require(Access::World(*this)==fixture_.Baseline() && PlayEditorScene(error),error.c_str());}
    void Update() override{Game::Update();++frames_;
        if(stopped_){const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Require(Access::Editing(*this) && Access::World(*this)==fixture_.Baseline() && !IsSceneDirty() && scene && !scene->GetModelRenderStatus(4).loaded,"Prefab Editor Stop retained runtime World/Model");Require(Access::EntityAudio(*this).SourceCount()==0 && Access::EntityAudio(*this).VoiceCount()==0 && Access::EntityAudio(*this).BufferBytes()==0 && Access::ScriptAudio(*this).OwnerCount()==0 && Access::ScriptAudio(*this).VoiceCount()==0,"Prefab Editor Stop retained Audio owners");finished_=true;return;}
        fixture_.Observe(Access::Entities(*this),Access::EntityAudio(*this),Access::ScriptAudio(*this),Access::Scripts(*this).GetLastError(1),Access::Scripts(*this).GetLastError(2),Access::Scripts(*this).GetLastError(4));
        if(fixture_.Done()){std::string error;Require(Access::Stop(*this,error),error.c_str());stopped_=true;}
    }
    void Draw() override{Game::Draw();++draws_;}
    bool IsEndRequest() const override{return finished_ || frames_>40 || Game::IsEndRequest();}
    void Finalize() override{Game::Finalize();retired_=true;}
    void Verify(){Require(finished_ && retired_ && draws_>=16,"Prefab Editor scenario incomplete");fixture_.VerifyGpu();std::cout<<"PASS: actual Editor Prefab spawn/new Model+Animator+Script+Physics+muted component/Lua Audio, failed-peer isolation, rendered frames and Play Stop baseline/Model/Audio retirement/GPU errors0\n";}
};
class AnimatorEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
public:
    void Initialize() override {
        fixture_.Prepare("Editor");SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");SetEditorMode(true);std::string error;
        Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());original_=Access::World(*this);
        Require(SelectEditorEntity(fixture_.controllerEntity,false,false,error),error.c_str());Access::OpenAnimatorInspector(*this);
        const auto guid=Access::Entities(*this).Find(fixture_.controllerEntity)->animator.controllerGuid;
        Require(OpenControllerAssetEditor(guid,error),error.c_str());
        auto& panel=Access::ControllerPanel(*this);sourceFile_=panel.Session().Source()->source;sourceBytes_=panel.Session().Source()->bytes;
        auto renamed=panel.Session().Document()->Definition().layers[0].states[0];renamed.name="Draft ### state";
        Require(Engine::Editor::AnimatorControllerEdits(*panel.Session().Document()).ReplaceState(0,0,renamed,error),error.c_str());draftText_=panel.Session().Document()->Text();
        Require(PlayEditorScene(error),error.c_str());
    }
    void Update() override {
        Game::Update();++updates_;std::string error;
        if(stopped_){Require(Access::Editing(*this) && Access::World(*this)==original_,"Animator Editor Stop did not preserve Editing snapshot");finished_=true;return;}
        fixture_.Observe(Access::Entities(*this),paused_?0:GetFrameDeltaTime(),Access::Scripts(*this).GetLastError(fixture_.fault));
        if(paused_){Require(Access::World(*this)==pausedWorld_,"Paused Animator Editor dispatched Script");paused_=false;Require(PlayEditorScene(error),error.c_str());return;}
        if(fixture_.stage==4 && !pauseChecked_){pausedWorld_=Access::World(*this);Access::Pause(*this);paused_=true;pauseChecked_=true;}
        if(fixture_.stage==8){
            auto& panel=Access::ControllerPanel(*this);Require(panel.CanvasFrames()>0 && panel.HasContext(),"Controller node canvas was never rendered");
            Access::StopForDocumentClose(*this);Require(Access::ControllerExitPending(*this) && !HasPendingDocumentAction(),"Dirty Controller did not gate Scene close");
            Require(ConfirmControllerClose(false,error) && !Access::ControllerExitPending(*this) && panel.Session().Document()->Text()==draftText_,"Cancel exit lost Controller draft");
            Access::StopForDocumentClose(*this);Require(ConfirmControllerClose(true,error) && !panel.HasContext() && !panel.Session().Document(),"Discard did not retire Controller owner/context");
            Require(Access::Editing(*this) && Access::World(*this)==original_,"Animator Editor document Stop lost authored snapshot");Require(Access::Scripts(*this).TakeAnimatorRequests().count==0,"Editor Stop retained Destroy Animator commands");const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Require(scene && scene->GetAnimatorRenderStatus(fixture_.owner).time==0 && !scene->GetAnimatorRenderStatus(fixture_.owner).playing && scene->GetAnimatorRenderStatus(fixture_.fixed).time==0 && !scene->GetAnimatorRenderStatus(fixture_.fixed).playing,"Actual Editor document Stop did not reset runtime clocks");stopped_=true;
            std::cout<<"PASS: actual Editor Controller node canvas/typed renamed draft/Scene close gate/Cancel retention/Discard context retirement/source+authored isolation\n";
        }
    }
    void Draw() override {
        Game::Draw();++draws_;
        if(draws_<=5){auto& panel=Access::ControllerPanel(*this);std::string error;
            if(draws_==1)Engine::Editor::ControllerCanvasChecksAccess::Position(panel,true);
            else {Engine::Editor::ControllerCanvasChecksAccess::Position(panel,false);
                auto& document=*panel.Session().Document();
                if(draws_==2){auto renamed=document.Definition().layers[0].states[0];renamed.name="Stable canvas history state";Require(Engine::Editor::AnimatorControllerEdits(document).ReplaceState(0,0,renamed,error),error.c_str());}
                if(draws_==3)Require(document.Undo(error),error.c_str());
                if(draws_==4)Require(document.Redo(error),error.c_str());
                draftText_=document.Text();
            }
        }
    }
    bool IsEndRequest() const override {return finished_ || updates_>25 || Game::IsEndRequest();}
    void Finalize() override {Game::Finalize();retired_=true;}
    void Verify(){Require(finished_ && pauseChecked_ && fixture_.stage==8 && draws_>=8 && retired_,"Animator Editor incomplete Pause/Stop/render/retirement");std::ifstream source(sourceFile_,std::ios::binary);Require(std::string(std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>())==sourceBytes_,"Controller draft changed source bytes");fixture_.VerifyGpu();}
private:
    AnimatorHostFixture fixture_;std::string original_,pausedWorld_,sourceBytes_,draftText_;std::filesystem::path sourceFile_;int updates_=0,draws_=0;bool paused_=false,pauseChecked_=false,stopped_=false,finished_=false,retired_=false;
};

#include "controller_owner_retry_editor_checks.h"
class ControllerSaveEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    AnimatorHostFixture fixture_;std::filesystem::path source_;std::string world_,saved_;int updates_=0;bool finished_=false,retired_=false;
    static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Require(stream.good(),"Controller Save native read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
    static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Require(!stream.fail(),"Controller Save native write failed");}
public:
    void Initialize() override {
        fixture_.Prepare("ControllerSaveEditor");SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");SetEditorMode(true);std::string error;
        std::cout<<"CONTROLLER_SAVE_NATIVE=initialize\n";
        Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());world_=Access::World(*this);
        std::cout<<"CONTROLLER_SAVE_NATIVE=initialized\n";
        const auto guid=Access::Entities(*this).Find(fixture_.controllerEntity)->animator.controllerGuid;Require(OpenControllerAssetEditor(guid,error),error.c_str());
        auto& session=Access::ControllerPanel(*this).Session();source_=session.Source()->source;saved_=Read(source_);
        auto state=session.Document()->Definition().layers[0].states[0];state.name="Saved ### Controller state";Require(Engine::Editor::AnimatorControllerEdits(*session.Document()).ReplaceState(0,0,state,error),error.c_str());
        Require(SelectEditorEntity(fixture_.controllerEntity,false,false,error) && !ReloadSelectedController(error),"Dirty draft reload bypassed guard");
    }
    void Update() override {
        std::cout<<"CONTROLLER_SAVE_NATIVE=update-"<<updates_<<'\n';Game::Update();std::cout<<"CONTROLLER_SAVE_NATIVE=updated\n";++updates_;if(updates_!=2 && updates_!=3)return;std::string error;auto& panel=Access::ControllerPanel(*this);auto& session=panel.Session();
        if(updates_==2){
            Require(panel.CanvasFrames()>0 && panel.HasContext(),"Save panel not rendered");
            std::cout<<"CONTROLLER_SAVE_NATIVE=play\n";Require(PlayEditorScene(error),error.c_str());std::cout<<"CONTROLLER_SAVE_NATIVE=playing\n";
            Require(!SaveControllerAsset(error) && Read(source_)==saved_ && panel.Dirty(),"Playing Save bypassed guard");
            Require(!ReloadSelectedController(error),"Playing reload bypassed guard");
            std::cout<<"CONTROLLER_SAVE_NATIVE=stop\n";Require(Access::Stop(*this,error),error.c_str());std::cout<<"CONTROLLER_SAVE_NATIVE=stopped\n";auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Require(scene,"Save Scene missing");scene->ResetAnimations();
            std::cout<<"CONTROLLER_SAVE_NATIVE=save\n";
            Require(Access::World(*this)==world_ && SaveControllerAsset(error) && !panel.Dirty() && !session.HasPendingAcknowledgement() && session.Document()->UndoCount()==1,"Actual Editor Save failed/lost history or authored World");
            saved_=session.Document()->Text();Require(Read(source_)==saved_ && std::filesystem::exists(session.LastArchive()/"pending.json"),"Actual Editor Save not published+archived");
            Require(scene->GetControllerState(fixture_.controllerEntity)=="Idle","Controller Save silently reloaded runtime owner");
            Require(scene->ControlAnimator(fixture_.controllerEntity,Engine::AnimationSystem::AnimatorCommand::Pause),"Reload Pause setup failed");
            Require(ReloadSelectedController(error) && scene->GetControllerState(fixture_.controllerEntity)=="Saved ### Controller state" &&
                !scene->GetAnimatorRenderStatus(fixture_.controllerEntity).playing && scene->GetAnimatorRenderStatus(fixture_.controllerEntity).time==0 &&
                Access::World(*this)==world_ && Read(source_)==saved_ && session.Document()->UndoCount()==1,"Explicit owner reload/reset/Pause/source+World+history isolation failed");
            Write(source_,"foreign source bytes");
            Require(!ReloadSelectedController(error) && scene->GetControllerState(fixture_.controllerEntity)=="Saved ### Controller state" &&
                !scene->GetAnimatorRenderStatus(fixture_.controllerEntity).playing && scene->GetAnimatorRenderStatus(fixture_.controllerEntity).time==0,"Stale source reload damaged old owner");
            Write(source_,saved_);
            auto state=session.Document()->Definition().layers[0].states[0];state.name="Second saved state";Require(Engine::Editor::AnimatorControllerEdits(*session.Document()).ReplaceState(0,0,state,error),error.c_str());
        }else{
            const auto draft=session.Document()->Text();Access::StopForDocumentClose(*this);Require(Access::ControllerExitPending(*this) && !HasPendingDocumentAction(),"Save close did not gate Scene");
            Require(ConfirmControllerClose(DocumentDecision::Cancel,error) && panel.Dirty() && session.Document()->Text()==draft,"Save close Cancel lost draft");
            Access::StopForDocumentClose(*this);Write(source_,"foreign source bytes");
            Require(!ConfirmControllerClose(DocumentDecision::Save,error) && Access::ControllerExitPending(*this) && !HasPendingDocumentAction() && panel.HasContext() && session.Document()->Text()==draft && Read(source_)=="foreign source bytes","Failed Save close lost draft/pending/context or overwrote foreign source");
            Write(source_,saved_);Require(ConfirmControllerClose(DocumentDecision::Save,error) && !Access::ControllerExitPending(*this) && !panel.HasContext() && !session.Document(),"Save close retry did not retire context/session");
            saved_=draft;Require(Read(source_)==saved_ && Access::World(*this)==world_,"Save close changed authored World or wrong source");finished_=true;
            std::cout<<"PASS: actual Editor Controller Save/canvas/playing guard/archive/history/no-auto-runtime-reload/Cancel/foreign conflict/retry Save close/context retirement/authored isolation\n";
        }
    }
    bool IsEndRequest() const override{return finished_ || updates_>10 || Game::IsEndRequest();}
    void Finalize() override{Game::Finalize();retired_=true;}
    void Verify(){Require(finished_ && retired_ && Read(source_)==saved_,"Controller Save native scenario incomplete");fixture_.VerifyGpu();}
};

class ControllerSaveAsEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    AnimatorHostFixture fixture_;std::string world_,original_,first_,pending_;int updates_=0,layoutWaitFrames_=0;bool finished_=false,retired_=false;
    std::string shortcutWorld_,shortcutName_;std::optional<Engine::Editor::EntityHistory> shortcutHistory_;
    static void FocusShortcut(const char* name){auto* window=ImGui::FindWindowByName(name);Require(window!=nullptr,"Actual Editor shortcut target missing");ImGui::FocusWindow(window);ImGui::ClearActiveID();}
    static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Require(stream.good(),"Save As native read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
    static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Require(!stream.fail(),"Save As native write failed");}
public:
    void Initialize() override {
        fixture_.Prepare("SaveAsEditor");SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");SetEditorMode(true);std::string error;
        Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());world_=Access::World(*this);original_=Read(fixture_.root/"Assets/scene-controller.animfsm");
        Require(NewControllerAssetEditor(error),error.c_str());auto& panel=Access::ControllerPanel(*this);Require(panel.Dirty() && panel.Session().Document()->IsUntitled() && !panel.Session().Source(),"Menu New did not open untitled draft");
        Require(!panel.SaveLayout(error) && !panel.RestoreSavedLayout(error) && !std::filesystem::exists(fixture_.root/"UserSettings/ControllerLayouts"),"Untitled layout bypassed source guard");
    }
    void Update() override {
        Game::Update();++updates_;if(updates_<2)return;std::string error;auto& panel=Access::ControllerPanel(*this);auto& session=panel.Session();auto& assets=Access::ControllerAssets(*this);
        if(updates_==2){
            Require(panel.CanvasFrames()>0 && panel.HasContext() && SaveControllerAssetAs("新規.animfsm",error),"Actual untitled Save As failed/canvas missing");
            Engine::Base::ProjectContext preferencesProject;Require(preferencesProject.Open(fixture_.root/"entry.kubo",error),error.c_str());
            const auto preferenceDocument=*session.Document();Require(panel.SetAutoSaveLayout(false,error),error.c_str());
            {Engine::Editor::AnimatorControllerPanel reopenedPreferences;Require(reopenedPreferences.ConfigureLayoutProject(preferencesProject,error) && reopenedPreferences.EditorPreferencesReady() && !reopenedPreferences.AutoSaveLayoutEnabled(),"Actual Editor preference did not survive fresh Panel");}
            Require(panel.SetAutoSaveLayout(true,error) && session.Document()->HasSameState(preferenceDocument) && Access::World(*this)==world_,"Actual preference toggle changed Controller/Scene or failed");
            std::cout<<"PASS: actual Editor project-owned Controller auto-save preference persists across fresh Panel without changing source/history/Scene\n";
            first_=session.Source()->guid;Require(!panel.Dirty() && !session.Document()->IsUntitled() && assets.FindByGuid(first_) && std::filesystem::exists(session.LastArchive()/"pending.json"),"Actual Save As not adopted+registered+archived");
            Engine::Editor::ControllerCanvasChecksAccess::Position(panel,true,true);
            const auto source=Read(session.Source()->source);Require(panel.SaveLayout(error) && Read(session.Source()->source)==source && Access::World(*this)==world_,error.c_str());
            Require(std::filesystem::exists(fixture_.root/"UserSettings/ControllerLayouts"/(first_+".json")),"Actual layout not saved under UserSettings");
            Require(panel.Close(false,error) && OpenControllerAssetEditor(first_,error) && !panel.HasContext(),"Actual layout close/reopen failed");
        }else if(updates_==3){
            Engine::Editor::ControllerCanvasChecksAccess::Position(panel,false);std::cout<<"PASS: actual Panel Save As layout persistence/close+reopen/new node context positions/source+World isolation\n";
            const auto copy=fixture_.root/"Assets/copy.animfsm";Write(copy,"foreign destination");
            Require(!SaveControllerAssetAs("copy.animfsm",error) && session.Source()->guid==first_ && Read(copy)=="foreign destination","Occupied Save As modified source/foreign file");std::filesystem::rename(copy,fixture_.root/"foreign-copy-retained");
            auto layer=session.Document()->Definition().layers[0];layer.weight=.5f;Require(Engine::Editor::AnimatorControllerEdits(*session.Document()).ReplaceLayer(0,layer,error),error.c_str());
            Require(SaveControllerAssetAs("copy.animfsm",error) && session.Source()->guid!=first_ && assets.FindByGuid(first_) && Read(fixture_.root/L"Assets/新規.animfsm")!=session.Document()->Text(),"Actual copy Save As changed original asset/GUID");
            Engine::Editor::ControllerCanvasChecksAccess::Position(panel,true,false,883,-212);
        }else if(updates_==4){
            if(Engine::Editor::ControllerCanvasChecksAccess::Pending(panel)){Require(++layoutWaitFrames_<180,"Quiet layout auto-save did not complete");--updates_;return;}
            Require(layoutWaitFrames_>0 && std::filesystem::exists(fixture_.root/"UserSettings/ControllerLayouts"/(session.Source()->guid+".json")),"Quiet layout auto-save never published");
            std::cout<<"PASS: actual Panel quiet debounce auto-saved moved node without explicit Save frames="<<layoutWaitFrames_<<'\n';
            Engine::Editor::ControllerCanvasChecksAccess::Position(panel,true,false,994,-313);const auto copiedGuid=session.Source()->guid;
            Require(panel.Close(false,error) && std::filesystem::exists(fixture_.root/"UserSettings/ControllerLayouts"/(copiedGuid+".json")) && OpenControllerAssetEditor(copiedGuid,error),"Auto-save close flush/reopen failed");
            Engine::Base::ProjectContext project;Require(project.Open(fixture_.root/"entry.kubo",error),error.c_str());
            Require(session.SaveAs(project,assets,"pending.animfsm",error,[](const auto&){throw std::runtime_error("native pending registration injection");})==Engine::Editor::AnimatorControllerSession::SaveOutcome::SavedPendingAcknowledgement,"Native pending fixture failed");
            pending_=session.Source()->guid;Require(panel.NeedsResolution() && !assets.FindByGuid(pending_) && !NewControllerAssetEditor(error),"Native pending creation lost guard/private registry");
            auto layer=session.Document()->Definition().layers[0];layer.weight=.75f;Require(Engine::Editor::AnimatorControllerEdits(*session.Document()).ReplaceLayer(0,layer,error),error.c_str());
            Require(SaveControllerAsset(error) && !session.HasPendingAcknowledgement() && !panel.Dirty() && assets.FindByGuid(pending_) && Read(fixture_.root/"Assets/pending.animfsm")==session.Document()->Text(),"Actual Save retry failed adoption/new draft save");
        }else if(updates_==5){Engine::Editor::ControllerCanvasChecksAccess::Position(panel,false,false,994,-313);std::cout<<"PASS: actual Panel moved-node autosave/clean close flush/reopen restored latest positions before debounce without explicit layout Save\n";Require(NewControllerAssetEditor(error) && session.Document()->IsUntitled() && !session.Source(),"New after Save As did not retire old draft");}
        else if(updates_==6){
            Require(Access::RenameForShortcut(*this,fixture_.controllerEntity,error),error.c_str());shortcutWorld_=Access::World(*this);shortcutHistory_=Access::History(*this);
            auto state=session.Document()->Definition().layers[0].states[0];shortcutName_=state.name;state.name="Keyboard draft";
            Require(Engine::Editor::AnimatorControllerEdits(*session.Document()).ReplaceState(0,0,state,error),error.c_str());
            FocusShortcut("Controller Graph");ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);
        }else if(updates_==7){ImGui::GetIO().AddKeyEvent(ImGuiKey_Z,true);}
        else if(updates_==8){
            Require(session.Document()->Definition().layers[0].states[0].name==shortcutName_ && Access::World(*this)==shortcutWorld_ && Access::History(*this).HasSameState(*shortcutHistory_),"Actual Controller key changed Scene or failed own Undo");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Z,false);ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);FocusShortcut("Entity Hierarchy");
        }else if(updates_==9){ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);}
        else if(updates_==10){ImGui::GetIO().AddKeyEvent(ImGuiKey_Z,true);}
        else if(updates_==11){
            Require(Access::World(*this)==world_ && session.Document()->Definition().layers[0].states[0].name==shortcutName_,"Actual Scene key changed Controller or failed own Undo");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Z,false);ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);FocusShortcut("Controller Graph");
        }else if(updates_==12){ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);}
        else if(updates_==13){ImGui::GetIO().AddKeyEvent(ImGuiKey_Y,true);}
        else if(updates_==14){
            Require(session.Document()->Definition().layers[0].states[0].name=="Keyboard draft" && Access::World(*this)==world_,"Actual Controller Redo escaped owner");
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Y,false);ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);
            std::cout<<"PASS: actual Editor Controller/Scene focused keyboard Undo+Redo target isolation with populated histories and unchanged peer World/history\n";
        }else if(updates_==15){
            Require(panel.HasContext(),"Untitled close canvas never rendered");Access::StopForDocumentClose(*this);
            Require(ConfirmControllerClose(DocumentDecision::Cancel,error) && session.Document()->IsUntitled(),"Untitled close Cancel lost draft");Access::StopForDocumentClose(*this);
            const auto destination=fixture_.root/"Assets/NewController.animfsm";Write(destination,"foreign initial-save destination");
            Require(!ConfirmControllerClose(DocumentDecision::Save,error) && Access::ControllerExitPending(*this) && session.Document()->IsUntitled() && panel.HasContext() && Read(destination)=="foreign initial-save destination","Failed untitled exit Save lost draft/context or overwrote destination");std::filesystem::rename(destination,fixture_.root/"foreign-initial-retained");
            Require(ConfirmControllerClose(DocumentDecision::Save,error) && !session.Document() && !panel.HasContext() && assets.FindByPath(destination),"Untitled exit Save did not create/register/retire");
            Require(Access::World(*this)==world_ && Read(fixture_.root/"Assets/scene-controller.animfsm")==original_,"New/Save As/retry changed authored Scene/original owner source");finished_=true;
            std::cout<<"PASS: actual Editor New/untitled canvas/Save As/new GUID+registry/archive/copy+occupied preservation/pending adoption+edited retry/untitled Cancel+failed exit Save+retry/context retirement/Scene owner isolation\n";
        }
    }
    bool IsEndRequest() const override{return finished_ || updates_>20 || Game::IsEndRequest();}
    void Finalize() override{Game::Finalize();retired_=true;}
    void Verify(){Require(finished_ && retired_,"Save As native scenario incomplete");fixture_.VerifyGpu();}
};

class ControllerRecoveryEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    AnimatorHostFixture fixture_;std::string original_,published_,world_,guid_,createdGuid_;int updates_=0;bool finished_=false,retired_=false;
    std::filesystem::path initialArchive_,partial_;std::string archiveGuid_=std::string(32,'d'),partialGuid_=std::string(32,'c');
    static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Require(stream.good(),"Recovery native read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
    static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Require(!stream.fail(),"Recovery native write failed");}
public:
    void Initialize() override {
        fixture_.Prepare("ControllerRecoveryEditor");Engine::Base::ProjectContext project;std::string error;Require(project.Open(fixture_.root/"entry.kubo",error),error.c_str());Engine::Assets::AssetRegistry assets;Require(assets.Scan(fixture_.root/"Assets",error,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        const auto source=fixture_.root/"Assets/scene-controller.animfsm";guid_=assets.FindByPath(source)->guid;Engine::Assets::AnimatorControllerRevision revision;
        {Engine::Assets::HeldAnimatorControllerAsset reader;Require(reader.Open(project,assets,guid_,error),error.c_str());revision=*reader.Snapshot();}original_=revision.bytes;
        Engine::Editor::AnimatorControllerDocument document;Require(document.Load(original_,error),error.c_str());auto layer=document.Definition().layers[0];layer.weight=.5f;Require(Engine::Editor::AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),error.c_str());published_=document.Text();
        {Engine::Editor::AnimatorControllerPublication publication;Require(publication.Prepare(project,assets,revision,published_,error) && publication.Publish(error),error.c_str());}
        {Engine::Editor::AnimatorControllerCreation creation;Require(creation.Prepare(project,"new-recovery.animfsm",original_,error) && creation.Publish(error),error.c_str());createdGuid_=creation.PublishedRevision()->guid;}
        initialArchive_=fixture_.root/"Assets/.kubo/controller-quarantine"/("publication-"+archiveGuid_+"-1-2-3");std::filesystem::create_directories(initialArchive_);
        Write(initialArchive_/"next.source",original_);Write(fixture_.root/"Assets/.kubo/controller-writers"/(archiveGuid_+".lock"),"");
        SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");SetEditorMode(true);Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());world_=Access::World(*this);
        Require(Access::ControllerInventorySize(*this)==2 && Read(source)==published_ && Read(fixture_.root/"Assets/new-recovery.animfsm")==original_,"Startup inventory automatically changed pending sources");
        Require(Access::ControllerArchiveSize(*this)==1 && Read(initialArchive_/"next.source")==original_,"Startup quarantine inventory changed archive");
    }
    void Update() override {
        Game::Update();++updates_;std::string error;using Family=Engine::Editor::ControllerPendingFamily;const auto source=fixture_.root/"Assets/scene-controller.animfsm";
        if(updates_==2){
            Require(Access::ControllerRecoveryFrames(*this)>0 && InspectControllerRecovery(Family::Publication,guid_,error),"Recovery window/inspection not connected");
            Require(NewControllerAssetEditor(error) && !RestoreControllerRecovery(error) && !CompleteControllerRecovery(error) && Read(source)==published_,"Live dirty draft did not guard recovery");Require(Access::ControllerPanel(*this).Close(true,error),error.c_str());
            Require(RestoreControllerRecovery(error) && Read(source)==original_ && Access::ControllerInventorySize(*this)==2,"Explicit original restore failed/implicitly acknowledged");
        }else if(updates_==3){
            Require(!InspectControllerRecovery(Family::Publication,std::string(32,'f'),error) && !CompleteControllerRecovery(error),"Failed new inspection retained wrong record authority");Require(InspectControllerRecovery(Family::Publication,guid_,error),error.c_str());
            Write(source,"foreign source bytes");Require(!RestoreControllerRecovery(error) && Read(source)=="foreign source bytes","Stale diagnostic recovery overwrote foreign source");Write(source,original_);
            Require(CompleteControllerRecovery(error) && Access::ControllerInventorySize(*this)==1 && Read(source)==original_,"Explicit publication acknowledgement/refresh failed");
            Require(OpenControllerAssetEditor(guid_,error),error.c_str());
            const auto baseline=*Access::ControllerPanel(*this).Session().Source();Engine::Base::ProjectContext project;Require(project.Open(fixture_.root/"entry.kubo",error),error.c_str());
            {Engine::Editor::AnimatorControllerPublication publication;Require(publication.Prepare(project,Access::ControllerAssets(*this),baseline,published_,error) && publication.Publish(error),error.c_str());}
            Require(InspectControllerRecovery(Family::Publication,guid_,error) && !RestoreControllerRecovery(error) && !CompleteControllerRecovery(error) && Read(source)==published_,"Clean affected open document did not guard inspected recovery");Require(Access::ControllerPanel(*this).Close(false,error),error.c_str());
            Require(RestoreControllerRecovery(error) && CompleteControllerRecovery(error) && Read(source)==original_,"Recovery after clean affected document close failed");
        }else if(updates_==4){
            Require(InspectControllerRecovery(Family::Creation,createdGuid_,error) && RestoreControllerRecovery(error) && !std::filesystem::exists(fixture_.root/"Assets/new-recovery.animfsm"),"Creation re-stage failed");
            Require(CompleteControllerRecovery(error) && Access::ControllerInventorySize(*this)==0 && !Access::ControllerAssets(*this).FindByGuid(createdGuid_) && Access::World(*this)==world_,"Creation abandonment archive/registry/Scene isolation failed");
        }else if(updates_==5){
            Require(InspectControllerRecord(Family::Publication,archiveGuid_,error,initialArchive_) && RequestControllerRecordAction(false,error) && ConfirmControllerRecordAction(false,error),error.c_str());
            Require(!Access::ControllerPanel(*this).Session().Document() && Read(initialArchive_/"next.source")==original_ && Access::World(*this)==world_,"Cancel changed draft/archive/World");
            Require(!ConfirmControllerRecordAction(true,error),"Cancelled action remained authorized");
            Require(RequestControllerRecordAction(false,error) && InspectControllerRecord(Family::Publication,archiveGuid_,error,initialArchive_) && !ConfirmControllerRecordAction(true,error),"Rediagnosed selection inherited earlier confirmation");
            Require(RequestControllerRecordAction(false,error) && ConfirmControllerRecordAction(true,error),error.c_str());
            auto& panel=Access::ControllerPanel(*this);Require(panel.Session().Document()->IsUntitled() && panel.Dirty() && !panel.Session().Source(),"Archive salvage did not reveal private draft");
            Require(InspectControllerRecord(Family::Publication,archiveGuid_,error,initialArchive_) && !RecoverControllerDraft(error),"Dirty Controller draft was replaced by salvage");
            Require(SaveControllerAssetAs("native-salvaged.animfsm",error) && panel.Session().Source()->guid!=archiveGuid_ && panel.Session().Source()->guid!=guid_ && !panel.NeedsResolution(),"Native salvage new-GUID SaveAs/adoption failed");
            Require(Read(initialArchive_/"next.source")==original_ && Read(source)==original_ && Access::World(*this)==world_ && !IsSceneDirty(),"Salvage changed archive/source/Scene");
            Require(panel.Close(false,error),error.c_str());
            partial_=fixture_.root/"Assets/.kubo/controller-transactions"/partialGuid_;std::filesystem::create_directories(partial_);
            Write(fixture_.root/"Assets/.kubo/controller-writers"/(partialGuid_+".lock"),"");Write(partial_/"next.source",original_);Write(partial_/"pending.json","{truncated");
        }else if(updates_==6){
            Require(InspectControllerRecord(Family::Publication,partialGuid_,error) && RequestControllerRecordAction(true,error),error.c_str());Write(partial_/"unknown","late child");
            Require(!ConfirmControllerRecordAction(true,error) && std::filesystem::exists(partial_),"Stale native confirmed quarantine admitted");
            Require(!InspectControllerRecord(Family::Publication,std::string(32,'f'),error) && !QuarantineControllerRecord(error) && !RecoverControllerDraft(error),"Failed partial diagnosis retained authority");
            Require(InspectControllerRecord(Family::Publication,partialGuid_,error) && RequestControllerRecordAction(true,error) && ConfirmControllerRecordAction(false,error) && std::filesystem::exists(partial_),"Cancelled quarantine changed record");
            Require(RequestControllerRecordAction(true,error) && ConfirmControllerRecordAction(true,error),error.c_str());
            Require(!std::filesystem::exists(partial_) && Access::ControllerArchiveSize(*this)==2 && Access::ControllerInventorySize(*this)==0 && Access::World(*this)==world_ && Read(source)==original_ && !IsSceneDirty(),"Native partial quarantine changed World/source/inventory");
        }else if(updates_==7){
            Require(Access::ControllerRecoveryFrames(*this)>0 && Access::ControllerArchiveSize(*this)==2 && Access::World(*this)==world_,"Next-frame partial archive/render isolation failed");finished_=true;
            std::cout<<"PASS: actual Editor quarantine startup/list, diagnosed archive -> shown untitled draft -> new GUID SaveAs/adoption, dirty/stale/failed diagnosis guards, partial quarantine/all inventory/Scene+source retention (command handlers + draw, not physical Confirm/Cancel gestures)\n";
            std::cout<<"PASS: actual Editor startup pending publication+creation inventory/visible recovery/no auto writes/Inspect/live draft guard/restore/stale refusal/explicit archive/failed inspect authority reset/creation re-stage+archive/registry+Scene isolation\n";
        }
    }
    bool IsEndRequest() const override{return finished_ || updates_>10 || Game::IsEndRequest();}
    void Finalize() override{Game::Finalize();retired_=true;}
    void Verify(){Require(finished_ && retired_,"Recovery native scenario incomplete");fixture_.VerifyGpu();}
};

class SceneTransitionEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
public:
    explicit SceneTransitionEditorChecks(bool button=false):button_(button){}
    ~SceneTransitionEditorChecks() override {Engine::InputSystem::captureInputChecksOverride=nullptr;}
    void Initialize() override {
        fixture_.Prepare(button_);
        SetProjectRoot(fixture_.root);
        SetProjectFile(fixture_.root/"entry.kubo");
        SetProjectAssetRoot(fixture_.root/"Assets");
        SetEditorMode(true);
        if(button_)SetGameOutputExtent({1280,720});
        std::string error;
        Require(ConfigureStartupScene(fixture_.root/"Assets/entry.scene",error),error);
        Game::Initialize();
        Access::HideAssetBrowser(*this);
        authored_=Access::World(*this);
        Require(PlayEditorScene(error),error);
        if(button_){
            Engine::InputSystem::captureInputChecksOverride=[](Engine::InputSystem::InputFrame& input,bool enabled) {
                RuntimeSceneTransitionFixture::ButtonInput(input,enabled,frames_);
            };
            ImGuiContextHook hook{};
            hook.Type=ImGuiContextHookType_NewFramePre;
            hook.Callback=[](ImGuiContext*,ImGuiContextHook*) {
                if(auto* window=ImGui::FindWindowByName("Game")){
                    const auto center=window->InnerRect.GetCenter();
                    ImGui::GetIO().AddMousePosEvent(RuntimeSceneTransitionFixture::ButtonTestOutside(frames_)?0:center.x,center.y+20);
                }
            };
            ImGui::AddContextHook(ImGui::GetCurrentContext(),&hook);
        }
    }
    void Update() override {
        if(button_ && frames_==7)ImGui::SetWindowFocus("Game");
        Game::Update();
        Require(++frames_<180,"Editor Scene transition timed out");
        if (stopped_) {
            Require(Access::World(*this)==authored_ && !IsSceneDirty() && Access::ScriptAudio(*this).VoiceCount()==0,
                "Stop after Scene transition changed the authored world or left audio");
            endRequest_=true;
        } else {
            heard_ |= Access::ScriptAudio(*this).VoiceCount()>0;
            if (fixture_.Arrived(Access::Entities(*this))) {
                if(button_ && !renderedHud_){
                    Require(frames_==24,"Editor activated the wrong button or accepted an incomplete click");
                    return;
                }
                if(!button_)Require(heard_,"Editor did not submit the trigger's WAV");
                std::string error;
                Require(Access::Stop(*this,error),error);
                stopped_=true;
                std::cout<<(button_?"PASS: Editor 16:9 Japanese title and text-only HUD pixels, Button cancellation and Stop snapshot\n":
                    "PASS: Editor movement, Trigger WAV, destination Lua initialization and Stop snapshot\n");
            }
        }
    }
    void Draw() override {
        Game::Draw();
        if(button_ && frames_==9)Require(fixture_.DrawnText(*dxCommon,*offscreenRenderManager,false),"Editor title label did not render");
        if(button_ && fixture_.Arrived(Access::Entities(*this))){
            Require(fixture_.DrawnText(*dxCommon,*offscreenRenderManager,true),"Editor HUD label did not render");
            renderedHud_=true;
        }
    }
private:
    RuntimeSceneTransitionFixture fixture_;
    std::string authored_;
    static inline int frames_=0;
    bool button_=false,renderedHud_=false;
    bool heard_=false,stopped_=false;
};

class FixedEditorHooksChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
public:
    ~FixedEditorHooksChecks() override {if(passed_){std::error_code error;std::filesystem::remove_all(fixture_,error);}else if(!fixture_.empty())std::cerr<<"Retained fixed Editor fixture: "<<fixture_<<'\n';}
    void Initialize() override {
        fixture_=std::filesystem::temp_directory_path()/(L"kubo-fixed-editor-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(fixture_/"Assets");
        {std::ofstream out(fixture_/"Assets/fixed.lua");out<<"function on_create(e) assert(scene.find('Fixed Editor probe')==e:id() and scene.entity_count()==4);e:translate(0,3,0);local x,y,z=scene.world_position(e:id());assert(y==3 and scene.world_matrix(e:id())[14]==3);local t=scene.find('Fixed command target');assert(scene.set_rotation(t,0,0.25,0) and scene.set_active(t,false));local born=scene.spawn('Fixed spawned');assert(born and scene.set_position(born,4,0,0)) end\nfunction on_fixed_update(e,dt) assert(scene.exists(e:id()));e:translate(dt,0,0);local x=e:position();assert(scene.world_matrix(e:id())[13]==x);local t=scene.find('Fixed command target');assert(scene.translate(t,2*dt,0,0));assert(scene.world_position(t)==scene.position(t));local temp=scene.spawn('Fixed temporary');assert(scene.destroy(temp) and not scene.exists(temp)) end\nfunction on_destroy(e) assert(scene.exists(e:id()));assert(scene.destroy(scene.find('Fixed spawned')));assert(scene.spawn('Destroy transient')) end\n";Require(bool(out),"Fixed Editor script write failed");}
        const auto modelDirectory=std::filesystem::current_path()/"Resources/models/axis";
        std::filesystem::copy(modelDirectory,fixture_/"Assets/models",std::filesystem::copy_options::recursive);
        {std::ofstream out(fixture_/"Assets/fixed.lua",std::ios::app);out<<"local original_create=on_create;function on_create(e) original_create(e);local id=scene.spawn_model('Fixed model','Assets/models/axis.obj');assert(id and scene.set_position(id,2,0,0)) end\nlocal original_fixed=on_fixed_update;function on_fixed_update(e,dt) original_fixed(e,dt);local id=scene.spawn_model('Fixed model temporary','Assets/models/axis.obj');assert(id and scene.destroy(id)) end\n";Require(bool(out),"Fixed Editor Model script write failed");}
        {std::ofstream out(fixture_/"entry.kubo");out<<R"({"name":"Fixed Editor","startupScene":"entry.scene"})";Require(bool(out),"Fixed Editor manifest write failed");}
        {std::ofstream trigger(fixture_/"Assets/trigger.lua");trigger<<
            "local n=0\nfunction on_trigger_enter(e,id) assert(id==scene.find('Trigger visitor')); n=n+1; e:set_rotation(0,0,n) end\n";
            Require(bool(trigger),"Cannot create Editor trigger-only Lua fixture");}
        Engine::Scene::EntityWorld world;const auto id=world.CreateEntity("Fixed Editor probe");world.SetScript(id,"Assets/fixed.lua");world.CreateEntity("Fixed command target");
        sensor_=world.CreateEntity("Editor trigger");
        const auto visitor=world.CreateEntity("Trigger visitor");
        world.Find(sensor_)->transform.position={0,100,0};
        world.Find(visitor)->transform.position={0,100,0};
        Require(world.SetCollider(sensor_,{{1,1,1},true}) && world.SetCollider(visitor,{{1,1,1},false}) &&
            world.SetScript(sensor_,"Assets/trigger.lua"),"Cannot author Editor trigger");
        std::string error;Require(world.Save(fixture_/"entry.scene",error),error.c_str());
        SetProjectRoot(fixture_);SetProjectFile(fixture_/"entry.kubo");SetProjectAssetRoot(fixture_/"Assets");SetEditorMode(true);Require(ConfigureStartupScene(fixture_/"entry.scene",error),error.c_str());Game::Initialize();
        // The Asset Browser worker has its own runner. Keep this frame-indexed
        // Pause/Resume scenario independent of the background import job.
        Access::HideAssetBrowser(*this);
        original_=Access::World(*this);Require(PlayEditorScene(error),error);
    }
    bool IsEndRequest() const override {return templateRegistered_ || Game::IsEndRequest();}
    void Update() override {
        Game::Update();++frames_;const auto usage=Access::PhysicsUsage(*this);ticks_+=usage.steps;Require(usage.steps<=Engine::Physics::PhysicsWorld::MaximumFrameSteps,"Editor fixed tick cap exceeded");
        std::string error;
        if(frames_==2){paused_=Access::World(*this);fraction_=usage.remainingSeconds;Engine::InputSystem::InputFrame press;press.available=true;press.pad.connected=true;press.pad.connectedThisFrame=true;press.pad.buttons[10]={true,true,false};Access::QueueInput(*this,std::move(press));Require(Access::FixedInputUsage(*this).pendingSamples==1,"Editor pending pad-only fixed input setup failed");Access::Pause(*this);}
        if(frames_==3){Require(usage.steps==0 && usage.remainingSeconds==fraction_ && Access::World(*this)==paused_ && !Access::FixedInputUsage(*this).pendingSamples,"Paused Editor executed fixed hooks/consumed clock or retained queued input");Require(PlayEditorScene(error),error);}
        if(frames_==7){
            const auto snapshot=Engine::Scene::SceneValue::parse(Access::World(*this));const auto& transform=snapshot["entities"][size_t(0)]["transform"]["position"];
            Require(ticks_>0 && std::abs(transform[size_t(0)].get<double>()-double(ticks_)*Engine::Physics::PhysicsWorld::FixedStepSeconds)<1e-4 && transform[size_t(1)].get<double>()==3,"Actual Editor fixed-only Lua dispatch/Create failed");
            const auto& peer=snapshot["entities"][size_t(1)];Require(std::abs(peer["transform"]["position"][size_t(0)].get<double>()-2*double(ticks_)*Engine::Physics::PhysicsWorld::FixedStepSeconds)<1e-4 && peer["transform"]["rotation"][size_t(1)].get<double>()==0.25 && !peer["active"].get<bool>(),"Actual Editor did not publish Create/fixed peer commands");
            Require(Access::Entities(*this).Find(sensor_)->transform.rotation.z==1,"Actual Editor repeated or omitted Trigger enter");
            const auto& records=snapshot["entities"].get<crude_json::array>();Require(records.size()==6 && records[4]["name"].get<std::string>()=="Fixed spawned" && records[4]["transform"]["position"][size_t(0)].get<double>()==4,"Actual Editor did not publish Lua spawn/delete or leaked fixed transient");
            const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            Require(records[5]["name"].get<std::string>()=="Fixed model" && records[5]["model"].get<std::string>()=="Assets/models/axis.obj" && scene && scene->GetModelRenderStatus(static_cast<Engine::Scene::EntityId>(records[5]["id"].get<double>())).loaded,"Actual Editor Lua Model generation did not publish/load renderer");
            Engine::InputSystem::InputFrame press;press.available=true;press.mouse[0]={true,true,false};press.pad.disconnectedThisFrame=true;press.pad.buttons[10]={false,false,true};Access::QueueInput(*this,std::move(press));Require(Access::FixedInputUsage(*this).pendingSamples==1,"Stop pending input setup failed");
            Require(Access::Stop(*this,error) && Access::World(*this)==original_ && Access::PhysicsUsage(*this).remainingSeconds==0 && !Access::FixedInputUsage(*this).pendingSamples,"Editor Stop lost authoring snapshot/fixed clock/input reset");
        }
        if(frames_>=8)Require(usage.steps==0 && Access::World(*this)==original_,"Editing after Stop executed fixed hooks");
        const auto path=fixture_/L"Assets/当たり判定.lua";
        if (frames_==8) {
            auto& world=Access::Entities(*this);
            Require(SelectEditorEntity(world.GetEntities().front().id,false,false,error),error);
            Require(!Access::CreateLuaScriptAsset(*this,fixture_/"outside.lua",error) &&
                !std::filesystem::exists(fixture_/"outside.lua"),"Lua editor accepted a destination outside Assets");
            Require(Access::CreateLuaScriptAsset(*this,path,error),error);
        }
        if (frames_>8 && !Access::ImportBusy(*this)) {
            const auto* asset=Access::ControllerAssets(*this).FindByPath(path);
            Require(asset && asset->type==Engine::Assets::AssetType::Script,"Template did not register as a Script asset");
            std::ifstream source(path);
            const std::string bytes{std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
            Require(bytes.find("function on_trigger_enter")!=std::string::npos &&
                bytes.find("function on_trigger_exit")!=std::string::npos,"Trigger template source missing hooks");
            Require(!Access::CreateLuaScriptAsset(*this,path,error),"Template creation overwrote an existing script");
            templateRegistered_=true;
            std::cout<<"PASS: Editor created and registered a Japanese Lua trigger template without overwriting files\n";
        }
        Require(frames_<120,"Editor template import timed out");
    }
    void Verify(){Require(frames_>=9 && templateRegistered_,"Fixed Editor runner stopped early");passed_=true;}
private:
    std::filesystem::path fixture_;std::string original_,paused_;double fraction_=0;size_t ticks_=0;int frames_=0;bool passed_=false;
    Engine::Scene::EntityId sensor_=0;
    bool templateRegistered_=false;
};

class RenderChecks final : public Engine::Scene::Game {
public:
    ~RenderChecks() override {std::error_code cleanup;std::filesystem::remove_all(fixture_,cleanup);}
    void Initialize() override {
        fixture_=std::filesystem::temp_directory_path()/(L"kubo-legacy-scene-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(fixture_);
        startup_=fixture_/L"場面/初回.scene";SetProjectRoot(fixture_);std::string error;
        Require(ConfigureStartupScene(startup_,error),error.c_str());Game::Initialize();
    }
    bool IsEndRequest() const override { return frames_ >= 8 || Game::IsEndRequest(); }
    void Update() override
    {
        Game::Update();
        ++frames_;
        if (frames_ == 2) {
            auto* scene = dynamic_cast<Engine::Scene::GamePlayScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            Require(scene != nullptr, "Sample scene must be active");
            Require(scene->GetScenePath()==startup_,"Missing authored startup path was lost by the sample factory");
            Require(!GetEditorSelection().empty(),"Legacy Editor hierarchy selection missing");
            const auto legacySelection=GetEditorSelection().front();const bool legacyDirty=IsSceneDirty();std::string selectionError;
            Require(SelectEditorEntity(legacySelection,true,false,selectionError) && GetEditorSelection().empty(),"Legacy Editor selection toggle rejected");
            Require(SelectEditorEntity(legacySelection,false,false,selectionError) && GetEditorSelection()==std::vector<Engine::Scene::EntityId>{legacySelection} && IsSceneDirty()==legacyDirty,"Legacy Editor selection replacement rejected/changed dirty state");
            std::cout<<"PASS: actual legacy sample Editor hierarchy selection toggle/replacement and unchanged dirty state\n";
            CheckSceneEditing(*scene);
        }
    }
    void Draw() override
    {
        Game::Draw();
        ++draws_;
    }
    void Verify()
    {
        Require(draws_ == 7, "Smoke test must actually render seven frames");
#ifdef _DEBUG
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages)))) {
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T size = 0;
                messages->GetMessage(i, nullptr, &size);
                std::vector<unsigned char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size))) {
                    Require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                        message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
                }
            }
        }
#endif
    }
private:
    std::filesystem::path fixture_,startup_;
    int frames_ = 0;
    int draws_ = 0;
};
}

namespace {
struct EditorDestroyFault {const void* token;Engine::Scene::EntityId id;};
class EditorShutdownHost final:public Engine::Scene::Game {
public:
    void Prime(){Engine::Base::Framework::Update();auto& scripts=Engine::Scene::GameApplicationChecksAccess::Scripts(*this);auto& world=Engine::Scene::GameApplicationChecksAccess::Entities(*this);scripts.Update(world,0.1f);scripts.FixedUpdate(world,0.1f);}
    auto* Graphics() const{return dxCommon.get();}
    auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    std::size_t Pending() const{return services_.PendingCount();}
};
std::atomic<unsigned> editorShutdownGpuErrors{0};
void CALLBACK ObserveEditorShutdownGpu(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++editorShutdownGpuErrors;
}
void CheckEditorShutdown(bool guarded){
    using Access=Engine::Scene::GameApplicationChecksAccess;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-editor-shutdown-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(fixture/"Assets");
    std::cout<<"Retained Editor shutdown fixture: "<<fixture<<'\n';
    {std::ofstream manifest(fixture/"entry.kubo");manifest<<R"({"name":"Owned shutdown","startupScene":"entry.scene"})";Require(bool(manifest),"Editor shutdown manifest failed");}
    Engine::Scene::EntityWorld seed;std::string error;
    for(unsigned index=0;index!=32;++index){const auto id=seed.CreateEntity("Owned Editor shutdown script");Require(seed.SetScript(id,"owned/shutdown"),"Editor shutdown seed failed");}
    Require(seed.Save(fixture/"entry.scene",error),error.c_str());
    auto host=std::make_unique<EditorShutdownHost>();
    struct HoldOnFailure {
        std::unique_ptr<EditorShutdownHost>& host;
        ~HoldOnFailure(){if(host && (host->Pending()!=0 || (host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired))){std::cerr<<"Refuse unsafe Editor fixture unwind\n";std::_Exit(81);}}
    } lifetime{host};
    host->SetProjectRoot(fixture);host->SetProjectFile(fixture/"entry.kubo");host->SetProjectAssetRoot(fixture/"Assets");host->SetEditorMode(true);host->SetMcpAtStartup(true);
    Require(host->ConfigureStartupScene(fixture/"entry.scene",error),error.c_str());
    std::array<unsigned,32> destroys{};unsigned first=32,reentries=0,retiredGpu=0;
    auto& scripts=Access::Scripts(*host);unsigned dispatchRefusals=0;
    auto checkDispatch=[&]{
        const auto used=host->Descriptors()->GetUsedCount();bool refused=false;
        try{host->Finalize();}catch(const std::logic_error&){refused=true;}
        Require(refused && scripts.IsDispatchActive() && host->Pending()==11 && IsWindow(host->Window()) && ImGui::GetCurrentContext() && Access::McpRunning(*host) && Access::ImportBusy(*host) &&
            std::filesystem::exists(Engine::Services::EditorMcpSession::Descriptor(fixture)) && host->Descriptors()->GetUsedCount()==used && host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Recording,"Editor Finalize changed owners during script dispatch");
        ++dispatchRefusals;
    };
    Engine::Scripting::ScriptCallbacks hooks;hooks.onCreate=[&](auto&){checkDispatch();};hooks.onUpdate=[&](auto&,float){checkDispatch();};hooks.onFixedUpdate=[&](auto&,float){checkDispatch();};
    hooks.onDestroy=[&](auto& entity){
        const auto index=static_cast<unsigned>(entity.id-1);if(first==32)first=index;++destroys.at(index);
        if(host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired)++retiredGpu;
        Require(!Access::McpRunning(*host) && !Access::ImportBusy(*host) && !std::filesystem::exists(Engine::Services::EditorMcpSession::Descriptor(fixture)),"Editor MCP/import ownership still active during Destroy");
        if(guarded){host->Finalize();++reentries;}
        Require(host->Pending()==11 && IsWindow(host->Window()) && ImGui::GetCurrentContext() && Engine::Scene::SceneManager::GetInstance()->GetCurrentScene(),"Editor callback/nested Finalize released borrowed World/Scene/UI services");
        throw EditorDestroyFault{host.get(),entity.id};
    };
    Require(scripts.Register("owned/shutdown",std::move(hooks)),"Editor shutdown registration failed");
    host->Initialize();ShowWindow(host->Window(),SW_HIDE);
    Require(Access::McpRunning(*host) && ImGui::GetCurrentContext(),"Actual Editor MCP/UI initialization missing");
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> messages;DWORD cookie=0;
    Require(SUCCEEDED(host->Graphics()->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages))) && SUCCEEDED(messages->RegisterMessageCallback(ObserveEditorShutdownGpu,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie)),"Editor shutdown GPU observer missing");
    struct Observer {ID3D12InfoQueue1* queue;DWORD cookie;~Observer(){queue->UnregisterMessageCallback(cookie);}} observer{messages.Get(),cookie};
    Require(Access::StartShutdownImport(*host,fixture) && Access::ImportBusy(*host),"Editor owned import future not acquired");
    host->Prime();const auto before=Access::World(*host);
    Require(!scripts.IsDispatchActive() && dispatchRefusals==96,"Editor active-dispatch scope/count mismatch");
    Require(scripts.GetRunningCount()==32 && host->Pending()==11,"Editor shutdown instances/services missing");
    bool original=false;try{host->Finalize();}catch(const EditorDestroyFault& fault){original=fault.token==host.get() && fault.id==first+1;}
    Require(original && scripts.GetRunningCount()==0 && host->Pending()==11 && IsWindow(host->Window()) && ImGui::GetCurrentContext() && Access::World(*host)==before,"Editor Destroy exception lost identity or retained authoring/UI ownership");
    Require(retiredGpu==(guarded?32u:0u) && reentries==(guarded?32u:0u),"Editor GPU-first/reentry contract mismatch");
    auto* graphics=host->Graphics();auto* descriptors=host->Descriptors();host->Finalize();host->Finalize();
    Require(host->Graphics()==graphics && host->Descriptors()==descriptors && host->Pending()==0 && descriptors->GetUsedCount()==0 && !IsWindow(host->Window()) && !ImGui::GetCurrentContext() && graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired && !Engine::Scene::SceneManager::GetInstance()->GetCurrentScene(),"Editor retry did not retire Scene/UI/services");
    for(auto count:destroys)Require(count==1,"Editor Destroy skipped/repeated");
    Require(editorShutdownGpuErrors==0,"Editor shutdown GPU Error/Corruption observed");host.reset();
    std::cout<<"EDITOR_DISPATCH_ADMISSION refused=96 owners_preserved=1 native_exit=0\n";
    std::cout<<"EDITOR_SHUTDOWN guarded="<<guarded<<" destroyed=32 reentries="<<reentries<<" mcp_stopped=1 ui_retained=1 gpu_errors=0 native_exit=0\n";
}
}

void RunSceneGridRenderChecks();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR arguments, int)
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    const auto mode=ApplicationCheckMode(arguments);
    if(mode=="--render-inspector"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{CheckGameRenderInspector();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--selection-inspector"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{CheckGameSelectionInspector();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--ui-inspector"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{CheckGameUIInspector();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--ui-move"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunSpriteMoveRenderChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--particle-authoring"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{CheckGameParticleInspector();RunParticleAuthoringChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--asset-thumbnails"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunAssetThumbnailChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--scene-grid"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunSceneGridRenderChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--editor-endurance"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<EditorEnduranceChecks>();checks->Run();checks->Verify();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--editor-toolbar"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<EditorToolbarChecks>();checks->Run();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--scene-asset-placement"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<SceneAssetPlacementChecks>();checks->Run();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--editor-input-focus" || mode=="--editor-input-focus-oversized"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<EditorInputFocusChecks>(mode=="--editor-input-focus-oversized");checks->Run();checks->Verify();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--particles"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunParticleGroupChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--lights"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunAuthoredLightingChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--prefab"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<PrefabEditorChecks>();checks->Run();checks->Verify();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(!mode.empty() && mode!="--controller-owner-retry" && mode!="--controller-recovery" && mode!="--controller-save-as" && mode!="--controller-save" && mode!="--startup-close" && mode!="--animator" && mode!="--gizmo-input" && mode!="--shaders" && mode!="--render" && mode!="--generic-views" && mode!="--mcp-export-guard" && mode!="--fixed-hooks" && mode!="--scene-transition" && mode!="--ui-button" && mode!="--git-index" && mode!="--shutdown") {std::cerr<<"Unknown Editor runner mode\n";return 1;}
    std::cout<<"RUNNER_MODE="<<mode<<'\n';
    if(mode=="--controller-owner-retry"){
        try{auto checks=std::make_unique<ControllerOwnerRetryEditorChecks>();checks->Run();checks->Verify();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--controller-recovery"){
        try{auto checks=std::make_unique<ControllerRecoveryEditorChecks>();checks->Run();checks->Verify();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--controller-save-as"){
        try{auto checks=std::make_unique<ControllerSaveAsEditorChecks>();checks->Run();checks->Verify();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--controller-save"){
        // Keep the new large host off this dispatcher's stack: existing modes
        // share its Debug stack frame and perform deeply nested initialization.
        try{auto checks=std::make_unique<ControllerSaveEditorChecks>();checks->Run();checks->Verify();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--startup-close") {
        try{auto checks=std::make_unique<StartupCloseEditorChecks>();checks->Run();checks->Verify();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--animator"){
        try{auto checks=std::make_unique<AnimatorEditorChecks>();checks->Run();checks->Verify();std::cout<<"PASS: actual Editor Lua Animator Create/Update/Fixed commands, failed-peer isolation, host Pause/document Stop snapshot+clock reset and Destroy queue clearing, real clock/render/GPU errors0 and owned shutdown\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--shutdown"){
        try{CheckEditorShutdown(true);return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--git-index") {
        try{auto checks=std::make_unique<GitEditorIndexChecks>();checks->Run();checks->Verify();std::cout<<"PASS: actual Editor async history + staged/unstaged diff + Stage/Unstage/Commit, read/write job exclusion and stale refusal, authored World/selection/disk isolation and dirty read admission (not physical buttons)\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--scene-transition" || mode=="--ui-button"){
        std::cout<<"RUNNER_MODE="<<mode<<'\n';
        try{auto checks=std::make_unique<SceneTransitionEditorChecks>(mode=="--ui-button");checks->Run();return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--fixed-hooks") {
        try{auto checks=std::make_unique<FixedEditorHooksChecks>();checks->Run();checks->Verify();std::cout<<"PASS: actual Editor fixed-only Lua, tick cap, Pause fraction/no-hook and Stop snapshot/clock/Editing isolation\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--mcp-export-guard") {
        try {auto checks=std::make_unique<McpExportGuardChecks>();checks->Run();checks->Verify();std::cout<<"PASS: actual running export child blocks MCP edit/save, permits read, then idle edit/Undo and owned cancellation\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if (mode == "--gizmo-input") {
        try { CheckGizmoInput(); return 0; }
        catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    }
    if (mode == "--shaders") {
        try { CheckShaderCache(); return 0; }
        catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    }
    if (mode == "--render") {
        try {
            auto render=std::make_unique<RenderChecks>();
            render->Run();
            render->Verify();
            std::cout << "PASS: seven rendered sample frames, DX12 debug messages, clean shutdown\n";
            return 0;
        } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    }
    if (mode == "--generic-views") {
        try { auto views=std::make_unique<GenericViewsChecks>(); views->Run(); views->Verify(); std::cout << "PASS: actual generic Editor Scene/Game dual-view 55-frame rendering, debounced resize, DX12 messages and shutdown\n"; return 0; }
        catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    }
    int result = 0;
    {
        ResourceChecks checks;
        checks.Initialize();
        try {
            checks.Check();
            std::cout << "PASS: GPU resource aliases, opaque keys, factory, animation dt, Console geometry\n";
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            result = 1;
        }
        checks.Finalize();
    }
    return result;
}
