#include "engine/assets/ExportPrefabDependencies.h"
#include <fstream>
#include <iostream>
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Write(const std::filesystem::path& p,const std::string& bytes){std::ofstream file(p,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);Need(file.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
int main(){namespace fs=std::filesystem;using namespace Engine;using namespace Assets;
    const auto root=fs::temp_directory_path()/(L"kubo-prefab-export-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(root/"Assets");fs::create_directories(root/"Resources");
        const auto manifest=root/"Game.kubo",file=root/"Assets/enemy.prefab",model=root/"Resources/triangle.obj",script=root/"Assets/behavior.lua",audio=root/"Assets/tone.wav",controller=root/"Assets/state.animfsm";
        Write(manifest,R"({"name":"Prefab export","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Write(model,"mtllib triangle.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");Write(root/"Resources/triangle.mtl","newmtl plain\nKd 1 1 1\n");Write(script,"return {on_update=function() end}");
        const unsigned char wave[]={ 'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0,0,0 };
        Write(audio,std::string(reinterpret_cast<const char*>(wave),sizeof(wave)));
        const std::string graph=R"({"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]})";
        Write(controller,graph);
        Scene::EntityWorld world;const auto id=world.CreateEntity("Enemy");Need(world.SetModel(id,"Resources/triangle.obj"),"Model setup");Need(world.SetScript(id,"native.enemy",true),"Script setup");auto* entity=world.Find(id);entity->hasAudio=true;entity->audio.assetPath="Assets/tone.wav";entity->hasAnimator=true;
        Prefab::PrefabValue definition;std::string error;Need(Prefab::CapturePrefabSubtree(world,id,definition,error),error.c_str());Write(file,definition.dump());
        AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());
        definition["world"]["entities"][0]["animator"]["controllerGuid"]=registry.FindByPath(controller)->guid;Write(file,definition.dump());
        const auto good=Read(file),metadata=Read(AssetRegistry::MetadataPath(file));
        const auto valid=[&](){return ValidateExportPrefabDependencies(manifest,{"Assets/enemy.prefab"},{"Assets","Resources"});};
        auto closure=valid();Need(closure.prefabs==1 && closure.opaqueScripts==1 && closure.models.files>=2,"Valid native callback/Model closure failed");
        Need(Read(file)==good && Read(AssetRegistry::MetadataPath(file))==metadata,"Validation mutated source/metadata");
        auto typed=definition;typed["world"]["entities"][0]["script"]["guid"]=registry.FindByPath(script)->guid;typed["world"]["entities"][0]["script"]["asset"]="Assets/stale.lua";Write(file,typed.dump());Need(valid().opaqueScripts==0,"Authoritative Script GUID with stale hint failed");
        const auto refuse=[&](){bool rejected=false;try{valid();}catch(const std::exception&){rejected=true;}Need(rejected,"Invalid Prefab export accepted");};
        auto bad=typed;bad["world"]["entities"][0]["script"]["guid"]=registry.FindByPath(audio)->guid;Write(file,bad.dump());refuse();
        bad=definition;bad["world"]["entities"][0]["model"]="Resources/missing.obj";Write(file,bad.dump());refuse();
        Write(file,good);Write(audio,"invalid Wave");refuse();Write(audio,std::string(reinterpret_cast<const char*>(wave),sizeof(wave)));
        Write(controller,"{}");refuse();Write(controller,graph);
        Write(file,"{\"root\":1,\"\\u0072oot\":2}");refuse();Write(file,std::string(Scene::SceneDocument::MaxBytes+1,'x'));refuse();Write(file,good);
        bool rejected=false;try{ValidateExportPrefabDependencies(manifest,{}, {"Assets","Resources"});}catch(const std::exception&){rejected=true;}Need(rejected,"Omitted inventory accepted");
        rejected=false;try{ValidateExportPrefabDependencies(manifest,{"Assets/enemy.prefab"},{"Assets"});}catch(const std::exception&){rejected=true;}Need(rejected,"Uncopied Model accepted");
        fs::rename(root/"Resources/triangle.mtl",root/"Resources/held.mtl");refuse();fs::rename(root/"Resources/held.mtl",root/"Resources/triangle.mtl");
        fs::create_hard_link(file,root/"held-prefab-alias");refuse();fs::remove(root/"held-prefab-alias");
        Need(valid().prefabs==1 && Read(file)==good,"Valid export not restored after refusals");
        const auto legacy=root/"Legacy";fs::create_directories(legacy/"Assets");Write(legacy/"Game.kubo",Read(manifest));
        Scene::EntityWorld empty;const auto legacyId=empty.CreateEntity("Legacy");Prefab::PrefabValue legacyDefinition;Need(Prefab::CapturePrefabSubtree(empty,legacyId,legacyDefinition,error),error.c_str());Write(legacy/"Assets/legacy.prefab",legacyDefinition.dump());
        Need(ValidateExportPrefabDependencies(legacy/"Game.kubo",{"Assets/legacy.prefab"},{"Assets"}).prefabs==1 && !fs::exists(AssetRegistry::MetadataPath(legacy/"Assets/legacy.prefab")),"Legacy export created metadata or refused");
        std::cout<<"PASS: Prefab export strict schema/inventory, nested Model closure, GUID Script/native callback compatibility, Wave/Controller refusal, copied roots/hardlinks, read-only modern+legacy files\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
