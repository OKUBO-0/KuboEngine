#include "engine/assets/ExportPrefabDependencies.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Export UI fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Export UI fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
int main(){using namespace Engine;using namespace Assets;std::filesystem::path root;try{
    root=std::filesystem::temp_directory_path()/(L"kubo-sprite-export-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(std::filesystem::create_directory(root),"Refuse existing export fixture");std::filesystem::create_directories(root/L"Assets/画像");std::filesystem::create_directory(root/"Resources");
    const auto project=root/"Game.kubo",scene=root/"entry.scene",image=root/L"Assets/画像/白.png",file=root/"Assets/ui.prefab";
    Write(project,R"({"name":"UI export","assetDirectory":"Assets","startupScene":"entry.scene"})");
    Write(image,"dependency fixture, not decoded image data");Write(root/"Assets/script.lua","return {}\n");Write(root/"Resources/outside.png","outside copy roots");
    Scene::EntityWorld world;const auto canvas=world.CreateEntity("Canvas"),a=world.CreateEntity("Sprite",canvas),b=world.CreateEntity("Disabled Sprite",canvas),draft=world.CreateEntity("Draft",canvas);
    Need(world.SetCanvas(canvas,{}) && world.SetSprite(draft,{}),"Export setup");
    AssetRegistry registry;std::string error;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(image)->guid,scriptGuid=registry.FindByPath(root/"Assets/script.lua")->guid;
    Scene::SpriteComponent sprite;sprite.assetGuid=guid;sprite.assetPath="Assets/stale-hint.png";Need(world.SetSprite(a,sprite),"Sprite setup");sprite.enabled=false;Need(world.SetSprite(b,sprite),"Disabled Sprite setup");world.Find(b)->active=false;
    Need(world.Save(scene,error),error.c_str());Prefab::PrefabValue prefab;Need(Prefab::CapturePrefabSubtree(world,canvas,prefab,error),error.c_str());Write(file,prefab.dump());Need(registry.Scan(root/"Assets",error),error.c_str());
    const auto originalScene=Read(scene),originalPrefab=Read(file),identity=Read(AssetRegistry::MetadataPath(image)),manifest=Read(project);
    const auto startup=[&](){return ValidateExportStartupSpriteDependencies(project,{"Assets"});};
    const auto templates=[&](){return ValidateExportPrefabDependencies(project,{"Assets/ui.prefab"},{"Assets"});};
    auto result=startup();Need(result.sprites==2 && result.textures==1 && result.unassigned==1,"Startup refs/dedup/disabled/draft closure");
    const auto closure=templates();Need(closure.prefabs==1 && closure.sprites.sprites==2 && closure.sprites.textures==1 && closure.sprites.unassigned==1,"Prefab Sprite closure missing");
    {auto aliases=world;auto first=sprite;first.assetGuid.clear();first.assetPath="Assets/画像/白.png";aliases.SetSprite(a,first);first.assetPath="ASSETS/画像/白.PNG";aliases.SetSprite(b,first);Need(ValidateExportSpriteDependencies(aliases,registry,root,{"Assets"}).textures==1,"Windows case aliases counted twice");}
    Need(Read(scene)==originalScene && Read(file)==originalPrefab && Read(AssetRegistry::MetadataPath(image))==identity && Read(project)==manifest,"Dependency verification wrote authored files");
    const auto refuse=[&](bool inPrefab){bool rejected=false;try{if(inPrefab)templates();else startup();}catch(const std::exception&){rejected=true;}Need(rejected,"Invalid Sprite export admitted");};
    for(bool inPrefab:{false,true}){
        const auto target=inPrefab?file:scene;
        const auto original=inPrefab?prefab:world.Capture();
        const auto edit=[&](const char* key,const Scene::SceneValue& value){auto changed=original;auto& entities=inPrefab?changed["world"]["entities"]:changed["entities"];entities[1]["sprite"][key]=value;Write(target,changed.dump());refuse(inPrefab);Write(target,inPrefab?originalPrefab:originalScene);};
        edit("guid",std::string(16,'f'));edit("guid",scriptGuid);edit("size",crude_json::array{-1.0,100.0});edit("asset","../escape.png");
        auto outside=original;auto& entities=inPrefab?outside["world"]["entities"]:outside["entities"];entities[1]["sprite"]["asset"]="Resources/outside.png";entities[1]["sprite"].get<crude_json::object>().erase("guid");Write(target,outside.dump());refuse(inPrefab);
        if(inPrefab)Need(ValidateExportPrefabDependencies(project,{"Assets/ui.prefab"},{"Assets","Resources"}).sprites.sprites==2,"Copied Prefab texture refused");
        else Need(ValidateExportStartupSpriteDependencies(project,{"Assets","Resources"}).sprites==2,"Copied Scene texture refused");
        Write(target,inPrefab?originalPrefab:originalScene);
    }
    auto changed=Scene::SceneValue::parse(identity);changed["guid"]=std::string(16,'e');Write(AssetRegistry::MetadataPath(image),changed.dump());refuse(false);refuse(true);Write(AssetRegistry::MetadataPath(image),identity);
    const auto held=root/"held.png";std::filesystem::rename(image,held);refuse(false);refuse(true);std::filesystem::rename(held,image);
    // A legacy path-only image remains supported and must not create a sidecar.
    const auto legacy=root/L"Assets/画像/legacy.png";Write(legacy,"legacy dependency fixture");sprite.assetGuid.clear();sprite.assetPath="Assets/画像/legacy.png";Need(world.SetSprite(a,sprite),"Legacy Sprite setup");Need(world.Save(scene,error),error.c_str());Need(startup().sprites==2 && !std::filesystem::exists(AssetRegistry::MetadataPath(legacy)),"Read-only export created legacy image metadata");Write(scene,originalScene);
    auto invalidCanvas=world.Capture();invalidCanvas["entities"][0]["canvas"]["referenceSize"]=crude_json::array{0.0,720.0};Write(scene,invalidCanvas.dump());refuse(false);Write(scene,originalScene);
    Need(startup().sprites==2 && templates().sprites.sprites==2 && Read(file)==originalPrefab && Read(AssetRegistry::MetadataPath(image))==identity,"Export did not recover/read-only invariant lost");
    std::filesystem::remove_all(root);root.clear();std::cout<<"PASS: Scene+Prefab Sprite export typed live GUID/stale hint, disabled/inactive refs, per-world texture dedup/unassigned drafts, source+metadata/copy roots/missing/replaced/wrong-type/invalid Canvas+Sprite refusal, read-only legacy image and recovery (not image decoding/packaged Player)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';if(!root.empty())std::cerr<<"SPRITE_EXPORT_FIXTURE_RETAINED="<<root<<'\n';return 1;}}
