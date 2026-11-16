#include "engine/assets/AssetImportJob.h"
#include "engine/assets/AssetSpriteDependencies.h"
#include "engine/assets/SpriteReference.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include <fstream>
#include <iostream>
#include <thread>
namespace {
namespace fs=std::filesystem;
using namespace Engine;
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Sprite move write failed");}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Sprite move read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
Assets::AssetImportJob::Result Wait(Assets::AssetImportJob& job){Assets::AssetImportJob::Result result;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!job.Poll(result)){Need(std::chrono::steady_clock::now()<deadline,"Sprite move worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}return result;}
}
int main(){fs::path root;const auto previous=fs::current_path();try{
 root=fs::temp_directory_path()/(L"kubo-sprite-move-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root),"Refuse existing Sprite move fixture");
 fs::create_directories(root/"Assets/Images");fs::create_directory(root/L"Assets/移動");fs::create_directory(root/"Scenes");fs::create_directory(root/"Unrelated");
 const auto image=root/L"Assets/Images/白.png",moved=root/L"Assets/移動/色.png",manifest=root/"selected.kubo",startup=root/"Scenes/start.json",prefab=root/"Assets/ui.prefab",closed=root/"Scenes/closed.scene";
 Write(image,"identity/import fixture, not decoded PNG");Write(manifest,R"({"name":"Sprite moves","assetDirectory":"Assets","startupScene":"Scenes/start.json"})");
 Assets::AssetRegistry registry;std::string error;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(image)->guid;const auto imageBytes=Read(image),metadata=Read(Assets::AssetRegistry::MetadataPath(image));
 Scene::EntityWorld world;const auto id=world.CreateEntity("Disabled assigned Sprite");Scene::CanvasComponent canvas;canvas.enabled=false;Need(world.SetCanvas(id,canvas),"Move Canvas setup");Scene::SpriteComponent sprite;sprite.enabled=false;sprite.assetPath="Assets/Images/白.png";sprite.assetGuid=guid;Need(world.SetSprite(id,sprite),"Move Sprite setup");
 Need(world.Save(startup,error),error.c_str());Prefab::PrefabValue definition;Need(Prefab::CapturePrefabSubtree(world,id,definition,error),error.c_str());Write(prefab,definition.dump());Write(closed,world.Capture().dump());Need(registry.Scan(root/"Assets",error),error.c_str());
 const auto inspect=[&]{auto graph=Assets::InspectAssetDependencies(registry);Assets::AppendProjectSpriteDependencies(graph,registry,root,manifest);return graph;};
 auto graph=inspect();Need(graph.issues.empty() && graph.DependentsOf(guid).size()==3 && graph.MoveRisk(registry,guid,"移動/色.png").empty(),"GUID Scene/Prefab/closed Scene dependencies blocked move or were omitted");
 Need(graph.MoveRisk(registry,registry.FindByPath(prefab)->guid,"移動/ui.prefab").empty(),"Project-root references incorrectly blocked moving the owning Prefab folder");
 fs::create_directory(root/"Scenes/Library");auto nested=world.Capture();nested["entities"].get<crude_json::array>()[0]["sprite"]["guid"]="";Write(root/"Scenes/Library/nested.scene",nested.dump());graph=inspect();Need(!graph.MoveRisk(registry,guid,"移動/色.png").empty(),"Legitimate nested Library Scene was skipped as project cache");fs::remove(root/"Scenes/Library/nested.scene");
 Need(Assets::OpenWorldSpriteMoveRisk(world,registry,root,guid).empty(),"Open world GUID guard");
 auto pathOnly=sprite;pathOnly.assetGuid.clear();Need(world.SetSprite(id,pathOnly),"Path-only setup");const auto beforeGuard=world.Capture().dump();Need(!Assets::OpenWorldSpriteMoveRisk(world,registry,root,guid).empty() && world.Capture().dump()==beforeGuard,"Disabled unsaved path-only Sprite missed or guard mutated world");
 Need(world.Save(startup,error),error.c_str());graph=inspect();Need(!graph.MoveRisk(registry,guid,"移動/色.png").empty(),"Explicit startup JSON path reference missed");
 Assets::AssetImportJob job;Assets::AssetImportJob::Request request{root,root/"Assets",{"Images/白.png","移動/色.png",guid},false,manifest};
 fs::current_path(root/"Unrelated");Need(job.Start(request),"Sprite move blocked-job start");auto result=Wait(job);Need(!result.success && !result.moveCommitted && fs::exists(image) && !fs::exists(moved) && !fs::exists(root/"Assets/.kubo/asset-move.pending.json") && Read(image)==imageBytes && Read(Assets::AssetRegistry::MetadataPath(image))==metadata,"Path-only guard mutated asset pair");
 Need(world.SetSprite(id,sprite) && world.Save(startup,error),error.c_str());Need(world.SetSprite(id,pathOnly) && world.Save(closed,error),error.c_str());graph=inspect();Need(!graph.MoveRisk(registry,guid,"移動/色.png").empty(),"Closed Scene path reference missed");
 Need(world.SetSprite(id,sprite) && world.Save(closed,error),error.c_str());Need(world.SetSprite(id,pathOnly),"Prefab path setup");Need(Prefab::CapturePrefabSubtree(world,id,definition,error),error.c_str());Write(prefab,definition.dump());graph=inspect();Need(!graph.MoveRisk(registry,guid,"移動/色.png").empty(),"Prefab path reference missed");
 Need(world.SetSprite(id,sprite),"GUID repair setup");Need(Prefab::CapturePrefabSubtree(world,id,definition,error),error.c_str());Write(prefab,definition.dump());
 const auto sceneBytes=Read(startup),closedBytes=Read(closed),prefabBytes=Read(prefab);auto oldRegistry=registry;Need(job.Start(request),"GUID move job start");result=Wait(job);Need(result.success && result.moveCommitted,result.error.c_str());
 auto other=request;other.projectFile=root/"other.kubo";Assets::AssetDependencies published;Need(!result.ApplyTo(registry,other,&published) && result.success,"Different selected manifest consumed completion");Need(result.ApplyTo(registry,request,&published) && !result.success,"Correct selected manifest failed to publish");
 Need(!fs::exists(image) && !fs::exists(Assets::AssetRegistry::MetadataPath(image)) && Read(moved)==imageBytes && Read(Assets::AssetRegistry::MetadataPath(moved))==metadata,"Sprite move lost content/metadata");
 Need(Assets::ResolveSpriteReference(sprite,registry,root,error)==fs::canonical(moved),"GUID did not follow actual worker move");Need(Assets::ResolveSpriteReference(sprite,oldRegistry,root,error).empty(),"Stale registry followed old path");
 graph=inspect();const auto firstFingerprints=Assets::BuildAssetImportFingerprints(registry,graph);Write(moved,"changed image bytes");const auto changedFingerprints=Assets::BuildAssetImportFingerprints(registry,graph);const auto prefabGuid=registry.FindByPath(prefab)->guid;Need(firstFingerprints.at(prefabGuid)!=changedFingerprints.at(prefabGuid),"Prefab import fingerprint omitted GUID Sprite image content");Write(moved,imageBytes);
 Need(Read(startup)==sceneBytes && Read(closed)==closedBytes && Read(prefab)==prefabBytes && sprite.assetPath=="Assets/Images/白.png","Move rewrote authored hint/documents");
 Scene::EntityWorld loaded;Need(loaded.Load(startup,error),error.c_str());Need(Assets::ResolveSpriteReference(loaded.Find(id)->sprite,registry,root,error)==fs::canonical(moved),"Reloaded Scene lost moved GUID");
 Scene::EntityWorld runtime;const auto owner=runtime.CreateEntity("Owner");Scene::EntityId born=0;Need(Prefab::InstantiateRuntimePrefab(runtime,definition,owner,born,error),error.c_str());Need(Assets::ResolveSpriteReference(runtime.Find(born)->sprite,registry,root,error)==fs::canonical(moved),"Runtime Prefab lost moved image GUID");
 Write(image,"competing old-name image");Write(Assets::AssetRegistry::MetadataPath(image),R"({"version":1,"guid":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"})");Need(Assets::ResolveSpriteReference(sprite,registry,root,error)==fs::canonical(moved),"Fresh GUID fell back to competing old filename");
 fs::create_directories(root/".kubo");Write(root/".kubo/broken.scene","{");graph=inspect();Need(graph.issues.empty(),"Excluded cache Scene contaminated dependency inspection");
 for(const auto& invalid:{std::string("{"),std::string("{\"entities\":[],\"entities\":[]}"),std::string(70,'[')+"0"+std::string(70,']'),std::string("{\"entities\":[{\"sprite\":{\"size\":[-1,1]}}]}"),std::string(4*1024*1024+1,' ')}){
  Write(startup,invalid);graph=inspect();Need(!graph.issues.empty() && !graph.MoveRisk(registry,guid,"Images/again.png").empty(),"Malformed/duplicate/deep/oversize Sprite dependency passed move admission");
 }Write(startup,sceneBytes);
 auto damaged=Scene::SceneValue::parse(sceneBytes);auto& entries=damaged["entities"].get<crude_json::array>();entries.push_back(entries.front());entries.back()["sprite"]["sortOrder"]=0.5;Write(startup,damaged.dump());graph=inspect();Need(!graph.issues.empty() && graph.DependentsOf(guid).size()==2,"Invalid file published a valid prefix of Sprite dependencies");Write(startup,sceneBytes);
 bool cancelled=false;try{Assets::AppendProjectSpriteDependencies(graph,registry,root,manifest,[]{return true;});}catch(const std::exception&){cancelled=true;}Need(cancelled,"Sprite inspection ignored cancellation");
 Write(root/"Assets/.kubo/asset-move.pending.json","pending");Need(Assets::ResolveSpriteReference(sprite,registry,root,error).empty(),"Pending move allowed Sprite GUID resolution");fs::remove(root/"Assets/.kubo/asset-move.pending.json");
 fs::current_path(previous);fs::remove_all(root);root.clear();std::cout<<"PASS: Sprite Scene/Prefab/closed+explicit JSON dependency kinds, disabled path guards/open World, real worker Unicode GUID move+publication/manifest ownership/reload/runtime Prefab/stale registry/old-name replacement, bounded malformed admission/cancellation and authored bytes preserved (not pixels/automatic path rewriting/concurrent writer safety)\n";return 0;
}catch(const std::exception& e){fs::current_path(previous);std::cerr<<"FAIL: "<<e.what()<<'\n';if(!root.empty())std::cerr<<"SPRITE_MOVE_FIXTURE_RETAINED="<<root<<'\n';return 1;}}
