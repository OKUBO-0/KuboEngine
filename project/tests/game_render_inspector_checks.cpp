#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "game_render_inspector_checks.h"
#include "engine/scene/game/Game.h"
#include "engine/editor/DebugEditorManager.h"
#include "engine/editor/SceneGuideProjection.h"
#include "engine/editor/RenderComponentInspector.h"
#include "engine/editor/UIComponentInspector.h"
#include <imgui_internal.h>
#include <Windows.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#if !defined(KUBO_APPLICATION_CHECKS)
#error The Game Inspector fixture must never be compiled into production applications
#endif
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Inspector source read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
struct Item {const char* label;ImVec2 center{};bool seen=false,disabled=false,mixed=false;};
std::array<Item,36> items{{{"Add Light"},{"Remove Light"},{"Add Material"},{"Remove Material"},{"Light Enabled"},{"Material Lighting"},
    {"Add Canvas"},{"Remove Canvas"},{"Canvas Enabled"},{"Add Sprite"},{"Remove Sprite"},{"Sprite Enabled"},
    {"Sprite Flip X"},{"Sprite Flip Y"},{"Clear Sprite Image"},{"Registered Sprite Image"},{"Sprite Texture Choice"},
    {"Button On Click"},{"Button Scene Choice"},
    {"Add Particle"},{"Remove Particle"},{"Particle Enabled"},{"Clear Particle Image"},{"Registered Particle Image"},{"Particle Texture Choice"},
    {"Hierarchy search"},{"Range First"},{"Range Last"},{"Active (selection)"},
    {"Hierarchy type filter"},{"Clear hierarchy filters"},{"Camera"},{"Unselected"},{"Child camera"},
    {"Add Component..."},{"Collider"}}};
void Observe(const char* label,ImVec2 a,ImVec2 b,bool disabled){for(auto& item:items)if(!std::strcmp(item.label,label)){item.center={(a.x+b.x)/2,(a.y+b.y)/2};item.seen=true;item.disabled=disabled;item.mixed=(ImGui::GetItemFlags()&ImGuiItemFlags_MixedValue)!=0;}}
struct GuiScope {
    ImGuiContext* previous=ImGui::GetCurrentContext();ImGuiContext* owned=ImGui::CreateContext();
    decltype(Engine::Editor::renderInspectorChecksObserver) previousObserver=Engine::Editor::renderInspectorChecksObserver;
    GuiScope(){try{Engine::Editor::renderInspectorChecksObserver=Observe;auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1800,1800};io.DeltaTime=1.f/60;unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);Need(pixels && width>0 && height>0,"Inspector font atlas missing");}catch(...){Engine::Editor::renderInspectorChecksObserver=previousObserver;ImGui::DestroyContext(owned);ImGui::SetCurrentContext(previous);throw;}}
    ~GuiScope(){Engine::Editor::renderInspectorChecksObserver=previousObserver;ImGui::DestroyContext(owned);ImGui::SetCurrentContext(previous);}
};
}
namespace Engine::Scene {
class GameRenderInspectorChecksAccess {
public:
    static void RunSelection() {
        Matrix4x4 projection{};
        projection.m[0][0] = projection.m[1][1] = projection.m[2][2] = projection.m[2][3] = 1;
        projection.m[3][2] = -0.5f;
        Vector2 firstPoint{}, lastPoint{};
        Need(Editor::ProjectSceneGuideLine({0.25f,0,-1},{0.25f,0,1},projection,firstPoint,lastPoint) &&
            std::abs(firstPoint.x-0.5f) < 0.001f && std::abs(lastPoint.x-0.25f) < 0.001f,
            "Scene guide did not clip at the near plane before projection");
        Need(!Editor::ProjectSceneGuideLine({0,0,-2},{0,0,-1},projection,firstPoint,lastPoint),
            "Scene guide projected geometry behind the camera");
        const auto root = std::filesystem::temp_directory_path() /
            (L"kubo-game-selection-inspector-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Need(std::filesystem::create_directory(root), "Selection fixture directory already exists");
        try {
            Game game;
            game.SetProjectRoot(root);
            game.SetEditorMode(true);
            game.genericWorldReady_ = game.genericSceneSelected_ = game.genericInspectorOpen_ = game.genericHierarchyOpen_ = true;
            const auto first = game.editorWorld_.CreateEntity("Range First");
            const auto middle = game.editorWorld_.CreateEntity("Range Middle");
            const auto last = game.editorWorld_.CreateEntity("Range Last");
            const auto outside = game.editorWorld_.CreateEntity("Unselected");
            const auto child = game.editorWorld_.CreateEntity("Child camera", outside);
            Need(game.editorWorld_.SetCamera(last, {}) && game.editorWorld_.SetCamera(child, {}),
                "Selection fixture camera assignment failed");
            for (const auto id : {middle, last, outside}) game.editorWorld_.Find(id)->active = false;
            game.genericWorldPath_ = root / "entry.scene";
            std::string error;
            Need(game.editorWorld_.Save(game.genericWorldPath_, error), "Selection fixture Scene save failed");
            game.documentState_.Opened(game.editorWorld_, game.genericWorldPath_);
            const auto baseline = Read(game.genericWorldPath_);
            const std::vector<EntityId> range{first, middle, last};
            GuiScope gui;
            const auto frame = [&] {
                for (auto& item : items) item.seen = false;
                ImGui::NewFrame();
                ImGui::SetWindowPos("Entity Hierarchy", {10, 10});
                ImGui::SetWindowSize("Entity Hierarchy", {410, 850});
                ImGui::SetWindowPos("Entity Inspector", {450, 10});
                ImGui::SetWindowSize("Entity Inspector", {600, 850});
                game.DrawGenericEntityEditor();
                ImGui::Render();
            };
            const auto item = [](const char* label) -> const Item& {
                for (const auto& candidate : items)
                    if (candidate.seen && !std::strcmp(label, candidate.label)) return candidate;
                throw std::runtime_error(std::string("Selection widget was not rendered: ") + label);
            };
            const auto click = [&](const char* label) {
                const auto point = item(label).center;
                auto& io = ImGui::GetIO();
                io.AddMousePosEvent(point.x, point.y);
                frame();
                io.AddMouseButtonEvent(0, true);
                frame();
                io.AddMouseButtonEvent(0, false);
                frame();
            };
            const auto undoOrRedo = [&](const char* window, ImGuiKey key) {
                if (window) ImGui::FocusWindow(ImGui::FindWindowByName(window));
                auto& io = ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                frame();
                io.AddKeyEvent(key, true);
                frame();
                io.AddKeyEvent(key, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
            };
            const auto active = [&](EntityId id) { return game.editorWorld_.Find(id)->active; };
            frame();
            frame();
            click("Range First");
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
            frame();
            click("Range Last");
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
            frame();
            Need(game.GetEditorSelection() == range, "Hierarchy Shift-click did not select the visible range");
            Need(item("Active (selection)").mixed && !item("Active (selection)").disabled,
                "Inspector did not show an editable mixed Active state");
            click("Active (selection)");
            Need(active(first) && active(middle) && active(last) && !active(outside),
                "Mixed Active edit did not enable exactly the selection");
            frame(); // The click frame calculated its mixed flag from the previous values.
            Need(game.IsSceneDirty() && game.entityHistory_.UndoCount() == 1 && !item("Active (selection)").mixed,
                "Mixed Active edit did not resolve the indicator and record one history entry");
            undoOrRedo("Entity Inspector", ImGuiKey_Z);
            Need(active(first) && !active(middle) && !active(last) && !active(outside) &&
                game.GetEditorSelection() == range && !game.IsSceneDirty() && item("Active (selection)").mixed,
                "Undo did not restore mixed Active values, selection and the saved baseline");
            undoOrRedo("Entity Inspector", ImGuiKey_Y);
            Need(active(middle) && active(last) && game.entityHistory_.UndoCount() == 1,
                "Redo did not restore the grouped Active edit");
            click("Hierarchy search");
            ImGui::GetIO().AddInputCharactersUTF8("Range First");
            frame();
            Need(ImGui::GetIO().WantTextInput && game.HasHierarchyHiddenSelection() && item("Active (selection)").disabled,
                "Hierarchy search did not take text focus and block edits on hidden selections");
            undoOrRedo(nullptr, ImGuiKey_Z);
            Need(active(middle) && active(last) && game.entityHistory_.UndoCount() == 1,
                "Ctrl+Z in the search field escaped into Scene history");
            click("Clear hierarchy filters");
            click("Hierarchy type filter");
            frame();
            frame();
            click("Camera");
            frame();
            const auto drawn = [](const char* label) {
                for (const auto& candidate : items)
                    if (!std::strcmp(label, candidate.label)) return candidate.seen;
                return false;
            };
            Need(game.hierarchyType_ == Game::HierarchyType::Camera && game.hierarchySearch_[0] == '\0',
                "Camera filter was not selected with an empty search");
            Need(!drawn("Range First") && drawn("Range Last") && drawn("Unselected") && drawn("Child camera"),
                "Camera filter did not keep matching rows and their ancestors");
            Need(game.HasHierarchyHiddenSelection() && item("Active (selection)").disabled &&
                !game.RunSceneEditAction(Editor::SceneEditAction::Delete) && game.GetEditorSelection() == range,
                "Hierarchy type filter allowed edits on hidden selection");
            click("Hierarchy search");
            ImGui::GetIO().AddInputCharactersUTF8("child");
            frame();
            Need(!drawn("Range Last") && drawn("Unselected") && drawn("Child camera"),
                "Name search did not combine with the type filter");
            click("Clear hierarchy filters");
            frame();
            Need(drawn("Range First") && drawn("Range Last") && !game.HasHierarchyHiddenSelection() &&
                !item("Active (selection)").disabled && game.entityHistory_.UndoCount() == 1,
                "Clearing filters did not restore selection editing without changing history");
            click("Range First");
            click("Add Component...");
            frame();
            frame();
            ImGui::GetIO().AddInputCharactersUTF8("coll");
            frame();
            Need(drawn("Collider") && !drawn("Camera"), "Component popup did not filter by name");
            click("Collider");
            frame();
            Need(game.editorWorld_.Find(first)->hasCollider && game.entityHistory_.UndoCount() == 2,
                "Component popup did not add a single undoable Collider");
            undoOrRedo("Entity Inspector", ImGuiKey_Z);
            Need(!game.editorWorld_.Find(first)->hasCollider && game.GetEditorSelection() == std::vector<EntityId>{first},
                "Undo of a component addition did not preserve the selection");
            Need(Read(game.genericWorldPath_) == baseline, "Inspector modified the saved Scene without saving");
            std::cout << "PASS: actual Hierarchy Shift-click, mixed Active, Undo/Redo, name/type filters, ancestor context, hidden-selection lock and search focus\n";
        } catch (...) {
            std::cerr << "GAME_SELECTION_INSPECTOR_FIXTURE_RETAINED=" << root << '\n';
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    static void RunParticle(){
        const auto root=std::filesystem::temp_directory_path()/(L"kubo-game-particle-inspector-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Need(std::filesystem::create_directory(root),"Refuse existing Particle Inspector fixture");std::filesystem::create_directory(root/"Assets");
        try {
            {std::ofstream out(root/"entry.kubo");out<<R"({"name":"Particle Inspector","startupScene":"entry.scene","assetDirectory":"Assets"})";}
            const auto image=root/L"Assets/白.png";{std::ofstream out(image);out<<"identity only, not decoded";}
            Game game;game.SetProjectRoot(root);game.SetProjectFile(root/"entry.kubo");game.SetProjectAssetRoot(root/"Assets");game.SetEditorMode(true);
            const auto id=game.editorWorld_.CreateEntity("Particle target");game.genericWorldReady_=game.genericSceneSelected_=game.genericInspectorOpen_=true;game.genericHierarchyOpen_=false;game.selectedEditorEntity_=id;game.editorSelection_.Replace(id);
            std::string error;Need(game.projectAssets_.Scan(root/"Assets",error),"Particle Inspector registry");const auto guid=game.projectAssets_.FindByPath(image)->guid;
            Need(game.editorWorld_.Save(root/"entry.scene",error),"Particle Inspector baseline save");game.genericWorldPath_=root/"entry.scene";game.documentState_.Opened(game.editorWorld_,game.genericWorldPath_);const auto original=Read(root/"entry.scene"),baseline=game.editorWorld_.Capture().dump();
            GuiScope gui;
            const auto frame=[&]{for(auto& item:items)item.seen=false;ImGui::NewFrame();if(auto* window=ImGui::FindWindowByName("Entity Inspector")){
                ImGui::SetWindowPos("Entity Inspector",{10,10});ImGui::SetWindowSize("Entity Inspector",{1000,1700});
                for(const char* label:{"Transform","Model","Animator","Camera","Script","Audio Source","Collider","RigidBody","Light","Material","Canvas","Sprite"})window->StateStorage.SetInt(window->GetID(label),0);
                window->StateStorage.SetInt(window->GetID("Particle"),1);
            }game.DrawGenericEntityEditor();ImGui::Render();};
            const auto click=[&](const char* label,bool disabled=false){frame();frame();const Item* selected=nullptr;for(const auto& item:items)if(!std::strcmp(item.label,label))selected=&item;Need(selected&&selected->seen&&selected->disabled==disabled,"Particle Inspector widget missing/lock mismatch");const auto center=selected->center;Need(center.x>0&&center.x<1800&&center.y>0&&center.y<1800,"Particle widget outside viewport");if(std::strcmp(label,"Particle Texture Choice"))ImGui::FocusWindow(ImGui::FindWindowByName("Entity Inspector"));auto& io=ImGui::GetIO();io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
            frame();frame();Need(game.editorWorld_.Capture().dump()==baseline&&!game.IsSceneDirty(),"Passive Particle Inspector dirty");
            click("Add Particle");Need(game.editorWorld_.Find(id)->hasParticle&&game.entityHistory_.UndoCount()==1&&game.IsSceneDirty(),"Particle Add/history");
            Need(game.ApplyEditorHistory(false,error)&&game.editorWorld_.Capture().dump()==baseline&&!game.IsSceneDirty(),"Particle Add Undo");Need(game.ApplyEditorHistory(true,error)&&game.editorWorld_.Find(id)->hasParticle,"Particle Add Redo");
            click("Registered Particle Image");click("Particle Texture Choice");click("Particle Enabled");Need(game.editorWorld_.Find(id)->particle.assetGuid==guid&&game.editorWorld_.Find(id)->particle.assetPath=="Assets/白.png"&&!game.editorWorld_.Find(id)->particle.enabled&&game.entityHistory_.UndoCount()==3,"Particle picker/enable/history");
            Need(game.CheckAssetMove(guid).empty(),"Particle GUID move refused");auto value=game.editorWorld_.Find(id)->particle;value.assetGuid.clear();Need(game.editorWorld_.SetParticle(id,value)&&!game.CheckAssetMove(guid).empty(),"Particle disabled unsaved path guard");value.assetGuid=guid;Need(game.editorWorld_.SetParticle(id,value),"Particle GUID repair");
            Need(game.SaveSceneAs(root/"saved.scene",error),"Particle Save As");EntityWorld saved;Need(saved.Load(root/"saved.scene",error)&&saved.Find(id)->particle==value&&!game.IsSceneDirty(),"Particle World2 save/reload");const auto authored=game.editorWorld_.Capture().dump();const auto history=game.entityHistory_;
            Need(game.PlayEditorScene(error),"Particle Play");click("Particle Enabled");Need(game.editorWorld_.Find(id)->particle.enabled&&!game.IsSceneDirty()&&game.entityHistory_.HasSameState(history),"Particle preview polluted history");
            Need(game.RequestNewScene(error)&&game.HasPendingDocumentAction()&&game.editorWorld_.Capture().dump()==authored,"Particle Stop snapshot restore");click("Remove Particle",true);click("Clear Particle Image",true);Need(game.editorWorld_.Capture().dump()==authored&&game.entityHistory_.HasSameState(history),"Particle pending lock bypass");Need(game.ConfirmDocumentAction(Game::DocumentDecision::Cancel,error),"Particle pending cancel");
            game.prefabRecoveryBlocked_=true;click("Remove Particle",true);Need(game.editorWorld_.Capture().dump()==authored,"Particle recovery lock bypass");game.prefabRecoveryBlocked_=false;
            click("Clear Particle Image");Need(game.editorWorld_.Find(id)->particle.assetPath.empty()&&game.editorWorld_.Find(id)->particle.assetGuid.empty()&&game.IsSceneDirty(),"Particle clear");Need(game.ApplyEditorHistory(false,error)&&game.editorWorld_.Capture().dump()==authored&&!game.IsSceneDirty(),"Particle clear Undo");
            click("Remove Particle");Need(!game.editorWorld_.Find(id)->hasParticle&&game.editorWorld_.Capture()["version"].get<double>()==1,"Particle removal version");Need(game.ApplyEditorHistory(false,error)&&game.editorWorld_.Capture().dump()==authored&&!game.IsSceneDirty()&&Read(root/"entry.scene")==original,"Particle Remove Undo/source preservation");
            std::cout<<"PASS: actual Game Particle Inspector mouse Add/Remove/enable/Unicode GUID picker/clear, grouped history+Undo/Redo, World2 Save As, Play/Stop restoration and pending/recovery locks (identity image only, not GPU)\n";
        }catch(...){std::cerr<<"GAME_PARTICLE_INSPECTOR_RETAINED="<<root<<'\n';throw;}
        std::error_code error;std::filesystem::remove_all(root,error);
    }
    static void Run(){
        const auto root=std::filesystem::temp_directory_path()/(L"kubo-game-render-inspector-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Need(std::filesystem::create_directory(root),"Refuse existing Inspector fixture");std::filesystem::create_directory(root/"Assets");bool passed=false;
        try {
            {std::ofstream out(root/"entry.kubo",std::ios::binary);out<<R"({"name":"Inspector","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets"})";out.close();Need(!out.fail(),"Inspector manifest write failed");}
            // The real Game owner/document/history code runs headlessly: no
            // Framework Initialize, Win32 input backend or GPU is substituted.
            Game game;game.SetProjectRoot(root);game.SetProjectFile(root/"entry.kubo");game.SetProjectAssetRoot(root/"Assets");game.SetEditorMode(true);
            const auto id=game.editorWorld_.CreateEntity("Inspector target");game.genericWorldReady_=game.genericSceneSelected_=game.genericInspectorOpen_=true;game.genericHierarchyOpen_=false;game.selectedEditorEntity_=id;game.editorSelection_.Replace(id);
            std::string error;Need(game.editorWorld_.Save(root/"entry.scene",error),error.c_str());game.genericWorldPath_=root/"entry.scene";game.documentState_.Opened(game.editorWorld_,game.genericWorldPath_);
            const auto original=Read(root/"entry.scene"),manifest=Read(root/"entry.kubo"),baseline=game.editorWorld_.Capture().dump();
            GuiScope gui;
            const auto frame=[&]{
                for(auto& item:items)item.seen=false;ImGui::NewFrame();
                if(auto* window=ImGui::FindWindowByName("Entity Inspector")){
                    ImGui::SetWindowPos("Entity Inspector",{10,10});ImGui::SetWindowSize("Entity Inspector",{900,1700});
                    for(const char* label:{"Transform","Model","Animator","Camera","Script","Audio Source","Collider","RigidBody","Canvas","Sprite"})window->StateStorage.SetInt(window->GetID(label),0);
                }
                game.DrawGenericEntityEditor();ImGui::Render();
            };
            const auto click=[&](const char* label,bool disabled=false){
                frame();frame();const Item* selected=nullptr;for(const auto& item:items)if(!std::strcmp(item.label,label))selected=&item;
                Need(selected && selected->seen && selected->disabled==disabled,"Actual Game Inspector widget missing or wrong disabled state");const auto center=selected->center;
                Need(center.x>0 && center.x<1800 && center.y>0 && center.y<1800,"Game Inspector widget outside fixture viewport");
                ImGui::FocusWindow(ImGui::FindWindowByName("Entity Inspector"));auto& io=ImGui::GetIO();io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
            };
            const auto press=[&](ImGuiKey key){
                ImGui::FocusWindow(ImGui::FindWindowByName("Entity Inspector"));ImGui::ClearActiveID();auto& io=ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddKeyEvent(key,true);frame();io.AddKeyEvent(key,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
            };
            frame();frame();Need(!game.IsSceneDirty() && !game.entityHistory_.CanUndo() && game.editorWorld_.Capture().dump()==baseline,"Passive Game Inspector dirtied document");
            click("Add Light");Need(game.editorWorld_.Find(id)->hasLight && game.IsSceneDirty() && game.entityHistory_.UndoCount()==1,"Game Light Add did not record/dirty once");
            press(ImGuiKey_Z);Need(!game.editorWorld_.Find(id)->hasLight && !game.IsSceneDirty(),"Focused Game Inspector Ctrl+Z did not restore saved baseline");
            press(ImGuiKey_Y);Need(game.editorWorld_.Find(id)->hasLight && game.IsSceneDirty(),"Focused Game Inspector Ctrl+Y failed");
            click("Add Material");Need(game.editorWorld_.Find(id)->hasMaterial && game.entityHistory_.UndoCount()==2,"Game Material Add did not record once");
            click("Material Lighting");Need(game.editorWorld_.Find(id)->material.lighting && game.entityHistory_.UndoCount()==3,"Game Material checkbox did not group once");
            Need(game.ApplyEditorHistory(false,error) && !game.editorWorld_.Find(id)->material.lighting && game.ApplyEditorHistory(true,error) && game.editorWorld_.Find(id)->material.lighting,"Game Material checkbox Undo/Redo failed");
            click("Light Enabled");Need(!game.editorWorld_.Find(id)->light.enabled && game.entityHistory_.UndoCount()==4,"Game Light checkbox did not group once");
            Need(game.SaveSceneAs(root/"saved.scene",error),error.c_str());Need(!game.IsSceneDirty() && game.GetOpenScenePath()==root/"saved.scene","Game render Save As did not set baseline/path");
            EntityWorld saved;Need(saved.Load(root/"saved.scene",error) && saved.Capture().dump()==game.editorWorld_.Capture().dump(),"Game render Save As lost components");
            const auto savedWorld=game.editorWorld_.Capture().dump();const auto history=game.entityHistory_;
            Need(game.PlayEditorScene(error),error.c_str());click("Material Lighting");Need(!game.editorWorld_.Find(id)->material.lighting && !game.IsSceneDirty() && game.entityHistory_.HasSameState(history),"Runtime Inspector edit polluted authored history/baseline");
            Need(game.RequestNewScene(error) && game.HasPendingDocumentAction() && game.editorWorld_.Capture().dump()==savedWorld,"Document New did not Stop/restore authored render baseline");
            click("Remove Light",true);click("Remove Material",true);press(ImGuiKey_Z);Need(game.editorWorld_.Capture().dump()==savedWorld && game.entityHistory_.HasSameState(history),"Pending document permitted render component/history edit");
            Need(game.ConfirmDocumentAction(Game::DocumentDecision::Cancel,error) && !game.HasPendingDocumentAction(),"Inspector pending document Cancel failed");
            game.prefabRecoveryBlocked_=true;click("Remove Light",true);click("Remove Material",true);press(ImGuiKey_Z);Need(game.editorWorld_.Capture().dump()==savedWorld && game.entityHistory_.HasSameState(history),"Constructed recovery guard permitted Inspector/history edit");game.prefabRecoveryBlocked_=false;
            click("Remove Light");click("Remove Material");Need(!game.editorWorld_.Find(id)->hasLight && !game.editorWorld_.Find(id)->hasMaterial && game.IsSceneDirty(),"Game render removals did not dirty document");
            Need(game.ApplyEditorHistory(false,error) && game.editorWorld_.Find(id)->hasMaterial && game.ApplyEditorHistory(false,error) && game.editorWorld_.Capture().dump()==savedWorld && !game.IsSceneDirty(),"Game render removal Undo did not restore saved baseline");
            Need(game.GetEditorSelection()==std::vector<EntityId>{id} && Read(root/"entry.scene")==original && Read(root/"entry.kubo")==manifest,"Inspector changed source/manifest/selection");
            passed=true;std::cout<<"PASS: actual Game headless Inspector mouse Add/Remove/checkbox, focused Ctrl+Z/Y and grouped history/dirty/Save As, runtime preview+document Stop restoration, pending-document/constructed recovery guards, source+selection preservation (not Win32/GPU/physical UI QA)\n";
        }catch(...){std::cerr<<"GAME_RENDER_INSPECTOR_FIXTURE_RETAINED="<<root<<'\n';throw;}
        if(passed){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    }
    static void RunUI(){
        const auto root=std::filesystem::temp_directory_path()/(L"kubo-game-ui-inspector-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Need(std::filesystem::create_directory(root),"Refuse existing UI Inspector fixture");std::filesystem::create_directory(root/"Assets");
        try {
            {std::ofstream out(root/"entry.kubo",std::ios::binary);out<<R"({"name":"UI Inspector","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets"})";out.close();Need(!out.fail(),"UI manifest write failed");}
            const auto image=root/L"Assets/白.png";{std::ofstream out(image,std::ios::binary);out<<"identity fixture only, not a decoded image";out.close();Need(!out.fail(),"UI image identity write failed");}
            const auto destination=root/L"Assets/遷移先.scene";
            EntityWorld next;next.CreateEntity("Destination");
            std::string sceneError;Need(next.Save(destination,sceneError),sceneError.c_str());
            for(const auto* file:{"shape.obj","step.lua","tone.wav"}){std::ofstream out(root/"Assets"/file,std::ios::binary);out<<"move reference fixture only";out.close();Need(!out.fail(),"Game asset guard fixture write failed");}
            Game game;game.SetProjectRoot(root);game.SetProjectFile(root/"entry.kubo");game.SetProjectAssetRoot(root/"Assets");game.SetEditorMode(true);
            const auto id=game.editorWorld_.CreateEntity("UI target");game.genericWorldReady_=game.genericSceneSelected_=game.genericInspectorOpen_=true;game.genericHierarchyOpen_=false;game.selectedEditorEntity_=id;game.editorSelection_.Replace(id);
            std::string error;Need(game.projectAssets_.Scan(root/"Assets",error),error.c_str());const auto* texture=game.projectAssets_.FindByPath(image);Need(texture && texture->type==Assets::AssetType::Texture,"UI image registry missing");const auto guid=texture->guid;
            Need(game.editorWorld_.Save(root/"entry.scene",error),error.c_str());game.genericWorldPath_=root/"entry.scene";game.documentState_.Opened(game.editorWorld_,game.genericWorldPath_);
            const auto original=Read(root/"entry.scene"),manifest=Read(root/"entry.kubo"),imageBytes=Read(image),metadata=Read(Assets::AssetRegistry::MetadataPath(image)),baseline=game.editorWorld_.Capture().dump();
            {const auto snapshot=game.editorWorld_.Capture();const auto h=game.entityHistory_;
             for(const auto* file:{"shape.obj","step.lua","tone.wav"}){
                Need(game.editorWorld_.Restore(snapshot,error),error.c_str());const auto* asset=game.projectAssets_.FindByPath(root/"Assets"/file);Need(asset,"Game asset guard registry missing");const auto hint=std::string("Assets/")+file;
                if(file[0]=='s' && file[1]=='h')Need(game.editorWorld_.SetModel(id,hint),"Game Model guard setup");
                else if(file[0]=='s')Need(game.editorWorld_.SetScript(id,hint,false),"Game Script guard setup");
                else {AudioSystem::AudioComponent audio;audio.assetPath=hint;audio.enabled=false;Need(game.editorWorld_.SetAudio(id,audio),"Game Audio guard setup");}
                const auto before=game.editorWorld_.Capture().dump();Need(!game.CheckAssetMove(asset->guid).empty() && game.editorWorld_.Capture().dump()==before && game.entityHistory_.HasSameState(h),"Actual Game missed unsaved Model/Script/Audio path reference");
                auto* entity=game.editorWorld_.Find(id);if(entity->hasModel)entity->model.assetGuid=asset->guid;else if(entity->hasScript)entity->script.assetGuid=asset->guid;else entity->audio.assetGuid=asset->guid;
                Need(game.CheckAssetMove(asset->guid).empty(),"Actual Game did not accept component GUID repair");
             }Need(game.editorWorld_.Restore(snapshot,error) && game.editorWorld_.Capture().dump()==baseline,error.c_str());
             std::cout<<"PASS: actual Game asset move guard for unsaved Model/disabled Script/disabled Audio paths, GUID repair and World/history preservation (reference admission, not file decoders/physical Browser)\n";}
            GuiScope gui;
            const auto frame=[&]{
                for(auto& item:items)item.seen=false;ImGui::NewFrame();
                if(auto* window=ImGui::FindWindowByName("Entity Inspector")){
                    ImGui::SetWindowPos("Entity Inspector",{10,10});ImGui::SetWindowSize("Entity Inspector",{1000,1700});
                    for(const char* label:{"Transform","Model","Animator","Camera","Script","Audio Source","Collider","RigidBody","Light","Material"})window->StateStorage.SetInt(window->GetID(label),0);
                    for(const char* label:{"Canvas","Sprite"})window->StateStorage.SetInt(window->GetID(label),1);
                }
                game.DrawGenericEntityEditor();ImGui::Render();
            };
            const auto click=[&](const char* label,bool disabled=false){
                frame();frame();const Item* selected=nullptr;for(const auto& item:items)if(!std::strcmp(item.label,label))selected=&item;
                if(!selected || !selected->seen || selected->disabled!=disabled)throw std::runtime_error(std::string("UI widget missing/disabled mismatch: ")+label);
                const auto center=selected->center;Need(center.x>0 && center.x<1800 && center.y>0 && center.y<1800,"UI widget outside fixture viewport");
                if(std::strcmp(label,"Sprite Texture Choice") && std::strcmp(label,"Button Scene Choice"))ImGui::FocusWindow(ImGui::FindWindowByName("Entity Inspector"));
                auto& io=ImGui::GetIO();io.AddMousePosEvent(center.x,center.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
            };
            const auto press=[&](ImGuiKey key){ImGui::FocusWindow(ImGui::FindWindowByName("Entity Inspector"));ImGui::ClearActiveID();auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddKeyEvent(key,true);frame();io.AddKeyEvent(key,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();};
            frame();frame();Need(!game.IsSceneDirty() && !game.entityHistory_.CanUndo() && game.editorWorld_.Capture().dump()==baseline,"Passive UI Inspector dirty");
            click("Add Canvas");Need(game.editorWorld_.Find(id)->hasCanvas && game.IsSceneDirty() && game.entityHistory_.UndoCount()==1,"Canvas Add/history failed");
            press(ImGuiKey_Z);Need(!game.editorWorld_.Find(id)->hasCanvas && !game.IsSceneDirty(),"Canvas CtrlZ failed");press(ImGuiKey_Y);Need(game.editorWorld_.Find(id)->hasCanvas,"Canvas CtrlY failed");
            click("Add Sprite");Need(game.editorWorld_.Find(id)->hasSprite && game.entityHistory_.UndoCount()==2,"Sprite Add/history failed");
            click("Sprite Flip X");click("Sprite Flip Y");click("Sprite Enabled");click("Canvas Enabled");
            Need(game.editorWorld_.Find(id)->sprite.flipX && game.editorWorld_.Find(id)->sprite.flipY && !game.editorWorld_.Find(id)->sprite.enabled && !game.editorWorld_.Find(id)->canvas.enabled && game.entityHistory_.UndoCount()==6,"UI checkboxes not grouped exactly once");
            click("Registered Sprite Image");click("Sprite Texture Choice");
            Need(game.editorWorld_.Find(id)->sprite.assetGuid==guid && game.editorWorld_.Find(id)->sprite.assetPath=="Assets/白.png" && game.entityHistory_.UndoCount()==7,"Real image picker did not assign Unicode hint/GUID once");
            Need(game.CheckAssetMove(guid).empty(),"Game move guard rejected GUID-backed Sprite");
            {const auto before=game.editorWorld_.Capture().dump();const auto historyBefore=game.entityHistory_;auto candidate=game.editorWorld_.Find(id)->sprite;candidate.assetGuid.clear();Need(game.editorWorld_.SetSprite(id,candidate),"Path-only Sprite fixture");
             const auto pending=game.editorWorld_.Capture().dump();Need(!game.CheckAssetMove(guid).empty() && game.editorWorld_.Capture().dump()==pending && game.entityHistory_.HasSameState(historyBefore),"Game move guard missed disabled unsaved path-only Sprite or mutated state");
             candidate.assetGuid=guid;Need(game.editorWorld_.SetSprite(id,candidate) && game.editorWorld_.Capture().dump()==before && game.CheckAssetMove(guid).empty(),"Game move guard did not recover after GUID assignment");}
            std::cout<<"PASS: actual Game asset-move admission blocks disabled unsaved path-only Sprite without World/history mutation and accepts registered Texture GUID repair (not physical Browser move/pixels)\n";
            Need(game.SaveSceneAs(root/"saved.scene",error),error.c_str());EntityWorld saved;Need(!game.IsSceneDirty() && saved.Load(root/"saved.scene",error) && saved.Capture().dump()==game.editorWorld_.Capture().dump(),"UI Save As lost data/baseline");
            const auto savedWorld=game.editorWorld_.Capture().dump();const auto history=game.entityHistory_;
            Need(game.PlayEditorScene(error),error.c_str());click("Sprite Enabled");Need(game.editorWorld_.Find(id)->sprite.enabled && !game.IsSceneDirty() && game.entityHistory_.HasSameState(history),"UI preview polluted authored history");
            Need(game.RequestNewScene(error) && game.HasPendingDocumentAction() && game.editorWorld_.Capture().dump()==savedWorld,"UI document Stop did not restore snapshot");
            click("Remove Canvas",true);click("Remove Sprite",true);click("Clear Sprite Image",true);press(ImGuiKey_Z);Need(game.editorWorld_.Capture().dump()==savedWorld && game.entityHistory_.HasSameState(history),"Pending document permitted UI edit");
            Need(game.ConfirmDocumentAction(Game::DocumentDecision::Cancel,error),error.c_str());
            game.prefabRecoveryBlocked_=true;click("Remove Canvas",true);click("Remove Sprite",true);press(ImGuiKey_Z);Need(game.editorWorld_.Capture().dump()==savedWorld && game.entityHistory_.HasSameState(history),"Recovery guard permitted UI edit");game.prefabRecoveryBlocked_=false;
            click("Clear Sprite Image");Need(game.editorWorld_.Find(id)->sprite.assetPath.empty() && game.editorWorld_.Find(id)->sprite.assetGuid.empty() && game.IsSceneDirty(),"Image clear failed");
            Need(game.ApplyEditorHistory(false,error) && game.editorWorld_.Capture().dump()==savedWorld && !game.IsSceneDirty(),"Image clear Undo lost GUID/source");
            click("Remove Sprite");click("Remove Canvas");Need(!game.editorWorld_.Find(id)->hasSprite && !game.editorWorld_.Find(id)->hasCanvas && game.IsSceneDirty(),"UI removal failed");
            Need(game.ApplyEditorHistory(false,error) && game.ApplyEditorHistory(false,error) && game.editorWorld_.Capture().dump()==savedWorld && !game.IsSceneDirty(),"UI removal Undo failed");
            click("Button On Click");click("Button Scene Choice");
            const auto* buttonScene=game.projectAssets_.FindByPath(destination);
            Need(buttonScene && game.editorWorld_.Find(id)->sprite.buttonSceneGuid==buttonScene->guid && game.IsSceneDirty(),
                "Button Inspector did not assign a Scene GUID");
            Need(game.ApplyEditorHistory(false,error) && game.editorWorld_.Capture().dump()==savedWorld && !game.IsSceneDirty(),
                "Button Scene assignment did not Undo in one step");
            Need(game.GetEditorSelection()==std::vector<EntityId>{id} && Read(root/"entry.scene")==original && Read(root/"entry.kubo")==manifest && Read(image)==imageBytes && Read(Assets::AssetRegistry::MetadataPath(image))==metadata,"UI Inspector changed selection/source/identity files");
            game.editorWorld_.Find(id)->canvas.enabled=true;
            auto& layoutSprite=game.editorWorld_.Find(id)->sprite;
            layoutSprite.enabled=true;layoutSprite.size={900,540};layoutSprite.anchor={0.5f,0.5f};
            game.uiLayoutOpen_=true;
            const auto projectOutput=game.GetGameOutputExtent();
            const auto layoutWorld=game.editorWorld_.Capture().dump();
            const auto layoutFrame=[&] {
                ImGui::NewFrame();ImGui::SetNextWindowPos({40,40},ImGuiCond_Always);
                game.DrawUILayout();ImGui::Render();
            };
            for (const int preset : {1,2}) {
                game.uiLayoutOutput_=preset;
                Need(game.SelectEditorEntity(0,false,false,error),error.c_str());
                layoutFrame();layoutFrame();
                auto* window=ImGui::FindWindowByName("UI Layout");
                Need(window,"UI Layout window did not open");
                ImGui::FocusWindow(window);
                const auto center=window->InnerRect.GetCenter();
                auto& io=ImGui::GetIO();
                io.AddMousePosEvent(center.x,center.y+12);layoutFrame();
                io.AddMouseButtonEvent(0,true);layoutFrame();
                io.AddMouseButtonEvent(0,false);layoutFrame();
                Need(game.GetEditorSelection()==std::vector<EntityId>{id} && game.genericInspectorOpen_,
                    "UI Layout preview did not select the visible Sprite");
            }
            Need(game.editorWorld_.Capture().dump()==layoutWorld && game.GetGameOutputExtent()==projectOutput,
                "UI Layout preview changed authored fields or project output");
            std::cout<<"PASS: UI Layout at 16:9 and 4:3 selects a Sprite in the Inspector without changing Scene data or project output\n";
            std::cout<<"PASS: actual Game headless Canvas/Sprite Inspector mouse Add/Remove/checkbox/Unicode Texture GUID picker/clear, CtrlZ/Y+grouped history/dirty/Save As, Play+document Stop restoration and pending/recovery guards; authored files preserved (not GPU/physical UI/Lua)\n";
        }catch(...){std::cerr<<"GAME_UI_INSPECTOR_FIXTURE_RETAINED="<<root<<'\n';throw;}
        std::error_code ignored;std::filesystem::remove_all(root,ignored);
    }
};
}
void CheckGameRenderInspector(){Engine::Scene::GameRenderInspectorChecksAccess::Run();}
void CheckGameSelectionInspector(){Engine::Scene::GameRenderInspectorChecksAccess::RunSelection();}
void CheckGameUIInspector(){Engine::Scene::GameRenderInspectorChecksAccess::RunUI();}
void CheckGameParticleInspector(){Engine::Scene::GameRenderInspectorChecksAccess::RunParticle();}
