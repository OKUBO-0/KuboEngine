#include "engine/script/ScriptAssetView.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine;using R=Scripting::ScriptAssetView::Result;
    const auto root=fs::temp_directory_path()/(L"kubo-script-prefab-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try{
        fs::create_directories(root/L"Assets/日本語");fs::create_directories(root/"Unrelated");
        const auto model=root/"Assets/shared.obj",script=root/"Assets/behavior.lua",audio=root/"Assets/sound.wav",controller=root/"Assets/states.animfsm",file=root/L"Assets/日本語/敵.prefab";
        Write(model,"v 0 0 0\n");Write(script,"return {}\n");Write(audio,"ownership fixture only, not Wave decoder proof");Write(controller,"{}");
        Scene::EntityWorld source;const auto id=source.CreateEntity("Enemy");Need(source.SetModel(id,"Assets/shared.obj"),"Model setup failed");Need(source.SetScript(id,"Assets/behavior.lua",true),"Script setup failed");
        auto* entity=source.Find(id);entity->hasAudio=true;entity->audio.assetPath="Assets/sound.wav";entity->hasAnimator=true;
        Prefab::PrefabValue definition;std::string error;Need(Prefab::CapturePrefabSubtree(source,id,definition,error),error.c_str());Write(file,definition.dump());
        Assets::AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto prefabGuid=registry.FindByPath(file)->guid,modelGuid=registry.FindByPath(model)->guid,scriptGuid=registry.FindByPath(script)->guid,audioGuid=registry.FindByPath(audio)->guid,controllerGuid=registry.FindByPath(controller)->guid;
        definition["world"]["entities"][0]["animator"]["controllerGuid"]=controllerGuid;Write(file,definition.dump());const auto original=Read(file);const auto metadata=Assets::AssetRegistry::MetadataPath(file);const auto originalMetadata=Read(metadata);
        Scripting::ScriptAssetView view(registry,root);Prefab::PrefabValue output;output["held"]=true;
        fs::current_path(root/"Unrelated");Need(view.Prefab("Assets/日本語/敵.prefab","",output)==R::Success,"Project-relative Unicode Prefab admission failed");
        Scene::EntityWorld admitted;Need(admitted.Restore(output["world"],error),error.c_str());const auto* admittedRoot=admitted.Find(id);
        Need(admittedRoot && admittedRoot->model.assetGuid==modelGuid && admittedRoot->script.assetGuid==scriptGuid && admittedRoot->audio.assetGuid==audioGuid && admittedRoot->animator.controllerGuid==controllerGuid,"Nested typed references not pinned on private copy");
        Need(view.Prefab("Assets/stale.prefab",prefabGuid,output)==R::Success && Read(file)==original && Read(metadata)==originalMetadata,"GUID stale hint did not resolve or source/metadata changed");
        Scene::EntityWorld destination;Scene::EntityId created=777;Need(Prefab::InstantiateRuntimePrefab(destination,output,0,created,error) && destination.Find(created)->model.assetGuid==modelGuid,"Admitted definition did not feed runtime factory");
        const auto held=output.dump();
        auto stale=definition;auto& staleNode=stale["world"]["entities"][0];staleNode["model"]="Assets/stale.obj";staleNode["modelGuid"]=modelGuid;staleNode["script"]["asset"]="Assets/stale.lua";staleNode["script"]["guid"]=scriptGuid;staleNode["audio"]["asset"]="Assets/stale.wav";staleNode["audio"]["guid"]=audioGuid;
        const auto staleBytes=stale.dump();Write(file,staleBytes);Need(view.Prefab("",prefabGuid,output)==R::Success && output.dump()==held && Read(file)==staleBytes,"Nested authoritative GUIDs did not repair hints on private copy only");Write(file,original);
        for(const auto& hint:std::vector<std::string>{"../outside.prefab","Assets/../bad.prefab","C:/outside.prefab","/outside.prefab","Assets/x:y.prefab","Assets/shared.obj","Assets/missing.prefab",std::string("Assets/a\0.prefab",16),std::string(1,char(255))})Need(view.Prefab(hint,"",output)!=R::Success && output.dump()==held,"Invalid top-level path changed admitted output");
        Need(view.Prefab("",modelGuid,output)==R::Invalid && view.Prefab("",std::string(32,'f'),output)==R::Missing && output.dump()==held,"Wrong/missing GUID fell back");
        const auto bad=[&](const std::string& bytes){Write(file,bytes);Need(view.Prefab("",prefabGuid,output)!=R::Success && output.dump()==held && Read(file)==bytes && Read(metadata)==originalMetadata,"Malformed/nested refusal changed output or files");};
        auto changed=definition;changed["world"]["entities"][0]["model"]="../outside.obj";bad(changed.dump());
        changed=definition;changed["world"]["entities"][0]["script"]["guid"]=audioGuid;bad(changed.dump());
        changed=definition;changed["world"]["entities"][0]["audio"]["asset"]="Assets/missing.wav";bad(changed.dump());
        changed=definition;changed["world"]["entities"][0]["animator"]["controllerGuid"]=scriptGuid;bad(changed.dump());
        bad("{\"root\":1,\"root\":2}");bad("{\"root\":1,\"\\u0072oot\":2}");bad("{\"x\":\"\\q\"}");bad(std::string(1,char(255)));bad(std::string(Scene::SceneDocument::MaxBytes+1,'x'));
        Write(file,original);Write(metadata,R"({"version":1,"guid":"00000000000000000000000000000000"})");Need(view.Prefab("",prefabGuid,output)==R::Invalid && output.dump()==held,"Stale metadata accepted");Write(metadata,originalMetadata);
        const auto scriptMeta=Assets::AssetRegistry::MetadataPath(script);const auto savedScriptMeta=Read(scriptMeta);Write(scriptMeta,"{}");Need(view.Prefab("",prefabGuid,output)==R::Invalid && output.dump()==held,"Nested changed identity accepted");Write(scriptMeta,savedScriptMeta);
        fs::create_directories(root/"Assets/.kubo");Write(root/"Assets/.kubo/asset-move.pending.json","{}");Need(view.Prefab("",prefabGuid,output)==R::Invalid && output.dump()==held,"Pending asset move accepted");fs::rename(root/"Assets/.kubo/asset-move.pending.json",root/"retained-move-intent.json");
        fs::create_directories(root/"Outside");Write(root/"Outside/other.prefab",original);Assets::AssetRegistry outside;Need(outside.Scan(root/"Outside",error),error.c_str());Scripting::ScriptAssetView escaped(outside,root/"Assets");Need(escaped.Prefab("",outside.FindByPath(root/"Outside/other.prefab")->guid,output)==R::Invalid && output.dump()==held,"Outside owner registry accepted");
        Need(CreateHardLinkW((root/"retained-prefab-alias.prefab").c_str(),file.c_str(),nullptr)!=FALSE,"Hardlink fixture unavailable");Need(view.Prefab("",prefabGuid,output)==R::Invalid && output.dump()==held,"Multi-link source admitted");fs::rename(file,root/"retained-prefab-shared.prefab");Write(file,original);
        const auto legacy=root/"Legacy";fs::create_directories(legacy/L"Assets/日本語");Write(legacy/"Assets/shared.obj",Read(model));Write(legacy/"Assets/behavior.lua",Read(script));Write(legacy/"Assets/sound.wav",Read(audio));
        auto legacyDefinition=definition;legacyDefinition["world"]["entities"][0]["animator"].erase("controllerGuid");Write(legacy/L"Assets/日本語/敵.prefab",legacyDefinition.dump());
        Assets::AssetRegistry legacyRegistry;Need(legacyRegistry.Scan(legacy/"Assets",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());Scripting::ScriptAssetView legacyView(legacyRegistry,legacy);Prefab::PrefabValue legacyOutput;
        Need(legacyView.Prefab("Assets/日本語/敵.prefab","",legacyOutput)==R::Success,"Read-only legacy path references refused");Scene::EntityWorld legacyWorld;Need(legacyWorld.Restore(legacyOutput["world"],error) && legacyWorld.Find(id)->model.assetGuid.empty() && legacyWorld.Find(id)->script.assetGuid.empty() && legacyWorld.Find(id)->audio.assetGuid.empty() && !fs::exists(Assets::AssetRegistry::MetadataPath(legacy/L"Assets/日本語/敵.prefab")) && !fs::exists(legacy/"Assets/.kubo"),"Legacy load invented persistent identities or wrote metadata");
        allocations=0;counting=true;const auto success=view.Prefab("",prefabGuid,output);counting=false;Need(success==R::Success,"Allocation baseline failed");const auto positions=allocations;
        for(size_t i=0;i<positions;++i){auto tested=output;failAfter=static_cast<long>(i);const auto result=view.Prefab("",prefabGuid,tested);failAfter=-1;Need(result!=R::Success && tested.dump()==held && Read(file)==original && Read(metadata)==originalMetadata,"Admission allocation failure published output or changed source");}
        fs::rename(root/"Assets",root/"Assets-held-check");fs::rename(root/"Assets-held-check",root/"Assets");
        fs::current_path(previous);std::cout<<"PASS: project-owned Prefab/Model/Script/Audio/Controller references, Unicode/cwd/GUID authority, private pinning/no source writes, read-only legacy identities, strict duplicate/escape/bounds and stale/nested/writer/multi-link guards, allocation isolation positions="<<positions<<'\n';return 0;
    }catch(const std::exception& e){failAfter=-1;counting=false;fs::current_path(previous);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
