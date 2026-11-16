#include "engine/app/RuntimeGame.h"
#include "engine/base/ProjectContext.h"
#include "engine/base/core/WinApp.h"
#include "engine/base/core/RunDiagnostics.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/core/SceneManager.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include "engine/script/ScriptSceneCommands.h"
#if defined(KUBO_SDK_RENDER_CHECKS)
#include "player_lighting_fixture.h"
#endif
#if defined(KUBO_SDK_UI_CHECKS)
#include "player_sprite_fixture.h"
#endif
#include <fstream>
#include <iostream>
#include <shellapi.h>
namespace {
namespace fs=std::filesystem;
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Write(const fs::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"SDK fixture write failed");}
std::string Read(const fs::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"SDK fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
fs::path Executable(){std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));Need(n && n<path.size(),"Executable path failed");path.resize(n);return path;}
void Prepare(const fs::path& root){
    using namespace Engine;Need(!fs::exists(root),"Use a new project fixture path");fs::create_directories(root/"Assets/models");
    fs::copy(Executable().parent_path()/"Resources/models/walk",root/"Assets/models",fs::copy_options::recursive);
    fs::copy_file(Executable().parent_path()/"Resources/textures/common/white.png",root/"Assets/models/white.png");
    Write(root/"Game.kubo",R"({"name":"SDK Prefab","assetDirectory":"Assets","startupScene":"entry.scene"})");
    const unsigned char wave[]={'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0,0,0};Write(root/"Assets/tone.wav",std::string(reinterpret_cast<const char*>(wave),sizeof(wave)));
    Write(root/"Assets/inner.lua",R"(local n=0
function on_create(e)
e:set_scale(1,1,1.25);audio.play('Assets/tone.wav',true,0)
assert(scene.remove_rigid_body(e:id()) and scene.rigid_body(e:id())==nil)
assert(scene.set_rigid_body(e:id(),2,0,0,3,true,false));e:set_velocity(1.5,0,0)
assert(scene.set_collider(e:id(),0.25,0.375,0.5,true,0.25,0.75))
assert(scene.set_collider(5,1,1,1,false,0,0));assert(scene.remove_collider(5))
assert(scene.set_collider(5,0.125,0.125,0.125,true,0,0))
local x,y,z=scene.world_position(e:id())
assert(scene.raycast(x-2,y,z,1,0,0,4)==e:id())
local hits=scene.overlap_box(x,y,z,0,0,0);assert(#hits==1 and hits[1]==e:id())
end
function on_update(e,dt) n=n+1;e:set_rotation(0,n/100,0);local x,y,z=scene.world_position(e:id());assert(scene.raycast(x-2,y,z,1,0,0,4)==e:id()) end
function on_fixed_update(e,dt) e:translate(0,0,0.001);local x,y,z=scene.world_position(e:id());assert(scene.overlap_box(x,y,z,0,0,0)[1]==e:id()) end
function on_destroy(e) e:set_rotation(0,0.75,0) end
)");
    Write(root/"Assets/driver.lua","function on_create(e) assert(scene.spawn_prefab('Assets/生成.prefab',e:id())) end\nfunction on_update(e,dt) end\n");
    Write(root/"Assets/fault.lua","function on_update(e,dt) e:translate(7,0,0);audio.stop_all();assert(scene.set_collider(e:id(),2,2,2,false,0.5,0.5));assert(scene.set_rigid_body(e:id(),9,0,0,2,true,false));assert(#scene.overlap_box(0,0,0,100,100,100)>0);for i=1,4 do assert(scene.spawn_prefab('Assets/heavy.prefab')) end end\n");
    Scene::EntityWorld templateWorld;const auto id=templateWorld.CreateEntity("SDK Prefab root"),child=templateWorld.CreateEntity("SDK Prefab child",id);
    Need(templateWorld.SetModel(id,"Assets/models/walk.gltf") && templateWorld.SetScript(id,"Assets/inner.lua"),"Prefab setup failed");auto* node=templateWorld.Find(id);
    node->transform.position={0,3,0};node->hasAnimator=true;node->animator.playOnAwake=true;node->hasCollider=node->hasRigidBody=true;node->rigidBody.useGravity=false;node->rigidBody.velocity={1,0,0};node->hasAudio=true;node->audio.assetPath="Assets/tone.wav";node->audio.loop=node->audio.playOnAwake=true;node->audio.volume=0;templateWorld.Find(child)->transform.position={0,1,0};
    Prefab::PrefabValue definition;std::string error;if(!Prefab::CapturePrefabSubtree(templateWorld,id,definition,error))throw std::runtime_error(error);const auto bytes=definition.dump();Write(root/L"Assets/生成.prefab",bytes);
    Need(bytes.size()<2*1024*1024,"SDK heavy Prefab fixture too large");Write(root/"Assets/heavy.prefab",bytes+std::string(2*1024*1024-bytes.size(),' '));
    Assets::AssetRegistry registry;if(!registry.Scan(root/"Assets",error))throw std::runtime_error(error);
    Scene::EntityWorld world;const auto driver=world.CreateEntity("Driver"),fault=world.CreateEntity("Fault"),camera=world.CreateEntity("Camera");Need(world.SetScript(driver,"Assets/driver.lua") && world.SetScript(fault,"Assets/fault.lua"),"Scene setup failed");world.Find(camera)->hasCamera=true;world.Find(camera)->transform.position={0,3,-8};if(!world.Save(root/"entry.scene",error))throw std::runtime_error(error);
}
class PrefabGame final:public Engine::App::RuntimeGame {
    int frames_=0,draws_=0;bool observed_=false;
public:
    void Update() override {
        RuntimeGame::Update();++frames_;if(frames_<5)return;
        const auto& world=GetWorld();const auto* root=world.Find(4);
        Need(world.GetEntities().size()==5 && root && world.Find(5) && root->parent==1 && world.Find(5)->parent==4 && root->prefabLink.is_null(),"SDK Prefab topology/fault isolation failed");
        Need(GetLuaError(1).empty() && GetLuaError(4).empty() && GetLuaError(2).find("Prefab admission 8 MiB")!=std::string::npos && world.Find(2)->transform.position.x==0,"SDK Prefab native work limit missing or fault prefix leaked");
        Need(!world.Find(2)->hasCollider && !world.Find(2)->hasRigidBody,"SDK failed Physics prefix leaked");
        Need(root->script.started && root->transform.scale.z==1.25f && root->transform.rotation.y>0 && root->transform.position.z>0 && root->transform.position.x>0,"SDK Prefab Create/Update/Fixed/Physics missing");
        Need(root->rigidBody.mass==3 && root->rigidBody.velocity.x==1.5f && !root->rigidBody.useGravity && root->collider.halfExtents.y==0.375f && root->collider.isTrigger && root->collider.friction==0.25f && root->collider.restitution==0.75f && world.Find(5)->collider.halfExtents.x==0.125f && world.Find(5)->collider.isTrigger,"SDK Lua Physics component replacement/removal/re-add missing");
        const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Need(scene && scene->GetModelRenderStatus(4).loaded,"SDK Prefab model owner missing");const auto animator=scene->GetAnimatorRenderStatus(4);Need(animator.ready && animator.playing && animator.time>0,"SDK Prefab Animator not advancing");
        Need(GetEntityAudioHost().SourceCount()==1 && GetEntityAudioHost().VoiceCount()==1 && GetEntityAudioHost().BufferBytes()==4 && GetEntityAudioHost().QueuedVoicesHaveVolume(4,0) && GetScriptAudioHost().OwnerCount()==1 && GetScriptAudioHost().VoiceCount()==1 && GetScriptAudioHost().BufferBytes()==4,"SDK Prefab separate Audio owners missing");
        observed_=true;if(frames_==24)PostMessageW(winApp->GetHwnd(),WM_CLOSE,0,0);
    }
    void Draw() override {RuntimeGame::Draw();++draws_;}
    void VerifyAfterRun() const {
        const auto* root=GetWorld().Find(4);Need(observed_ && draws_>=23 && root && !root->script.started && root->transform.rotation.y==0.75f,"SDK Prefab Destroy/render verification missing");
        Need(GetEntityAudioHost().SourceCount()==0 && GetEntityAudioHost().VoiceCount()==0 && GetEntityAudioHost().BufferBytes()==0 && GetScriptAudioHost().OwnerCount()==0 && GetScriptAudioHost().VoiceCount()==0 && GetScriptAudioHost().BufferBytes()==0,"SDK Prefab cleanup incomplete");
    }
};
void NativeApi(const fs::path& root){
    using namespace Engine;Assets::AssetRegistry registry;std::string error;if(!registry.Scan(root/"Assets",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);Scripting::ScriptAssetView assets(registry,root);Prefab::PrefabValue definition;
    Need(assets.Prefab("Assets/生成.prefab","",definition)==Scripting::ScriptAssetView::Result::Success,"SDK native asset admission failed");Scene::EntityWorld world;world.CreateEntity("Owner");Scene::EntityId born=0;Need(Prefab::InstantiateRuntimePrefab(world,definition,1,born,error) && born==2 && world.Find(3)->parent==2,"SDK native factory header/archive mismatch");
    auto view=std::make_shared<Scripting::ScriptSceneView>();auto detached=*world.Find(1);Scripting::ScriptSceneCommands commands;
    {Scripting::ScriptSceneView::WriteScope scope(*view,world,&assets);commands.Begin(view.get(),detached);using R=Scripting::ScriptSceneCommands::Result;
     Need(commands.SpawnPrefab("Assets/生成.prefab","",1,born)==R::Success && commands.CreatedCount()==2 && commands.PrefabAdmissionUsage().bytes>0 && !commands.PrefabAdmissionUsage().exceeded,"SDK native Prefab command/work accounting failed");
     Physics::RigidBodyComponent settings;settings.mass=3;settings.velocity={1.5f,0,0};settings.useGravity=false;
     Physics::ColliderComponent collider;collider.isTrigger=true;Physics::RigidBodyComponent observed;
     Need(commands.SetRigidBody(1,settings)==R::Success && commands.RigidBody(1,observed) && observed.mass==3 && observed.velocity.x==1.5f && commands.SetCollider(1,collider)==R::Success && commands.Collider(1)->isTrigger && commands.RemoveCollider(1)==R::Success && !commands.Collider(1),"SDK native Physics component API/header/archive mismatch");
     Physics::PhysicsRayHit hit;std::vector<Scene::EntityId> overlaps;
     Need(commands.Raycast({-2,3,0},{1,0,0},4,hit)==R::Success && hit.entity==2 && commands.OverlapBox({0,3,0},{0,0,0},overlaps)==R::Success && overlaps==std::vector<Scene::EntityId>{2,4},"SDK native staged ray/overlap header/archive mismatch");
     Need(commands.Prepare() && commands.PublishPrepared(),"SDK native Physics query/command publication failed");
    }
    Need(born==4 && world.Find(5)->parent==4 && world.Find(1)->hasRigidBody && world.Find(1)->rigidBody.mass==3 && !world.Find(1)->hasCollider,"SDK native Prefab/Physics publication failed");
}
}
int wmain(int argc,wchar_t** argv){
    try{
#if defined(KUBO_SDK_UI_CHECKS)
        if(argc==3 && std::wstring(argv[1])==L"--prepare-ui"){PreparePlayerLuaUIProject(fs::absolute(argv[2]));std::cout<<"PASS: SDK Canvas/Sprite fixture prepared\n";return 0;}
        if(argc>=2 && std::wstring(argv[1])==L"--ui"){
            Need(argc==2 || (argc==4 && std::wstring(argv[2])==L"--project"),"Invalid SDK UI arguments");
            const auto manifest=argc==4?fs::absolute(argv[3]):Engine::Base::ProjectContext::FindManifestUpwards(Executable());
            RunPlayerLuaUIProject(manifest);std::cout<<"PASS: SDK-only Canvas/Sprite Lua lifecycle/RGB pixels/multiple swapchain indices/rollback/source preservation/GPU errors0/shutdown\n";return 0;
        }
#endif
#if defined(KUBO_SDK_RENDER_CHECKS)
        if(argc==3 && std::wstring(argv[1])==L"--prepare-render"){PreparePlayerLightingProject(fs::absolute(argv[2]));std::cout<<"PASS: SDK Light/Material fixture prepared\n";return 0;}
        if(argc>=2 && std::wstring(argv[1])==L"--render"){
            Need(argc==2 || (argc==4 && std::wstring(argv[2])==L"--project"),"Invalid SDK render arguments");
            const auto manifest=argc==4?fs::absolute(argv[3]):Engine::Base::ProjectContext::FindManifestUpwards(Executable());
            RunPlayerLightingProject(manifest);std::cout<<"PASS: SDK-only Light/Material Lua lifecycle/3-kind pixels/multiple swapchain indices/rollback/source preservation/GPU errors0/shutdown\n";return 0;
        }
#endif
        if(argc==3 && std::wstring(argv[1])==L"--prepare"){Prepare(fs::absolute(argv[2]));std::cout<<"PASS: SDK Prefab fixture prepared\n";return 0;}
        auto manifest=argc==3 && std::wstring(argv[1])==L"--project"?fs::absolute(argv[2]):Engine::Base::ProjectContext::FindManifestUpwards(Executable());Engine::Base::ProjectContext project;std::string error;if(!project.Open(manifest,error))throw std::runtime_error(error);const auto root=project.GetRootPath();const auto scene=Read(root/"entry.scene"),prefab=Read(root/L"Assets/生成.prefab"),heavy=Read(root/"Assets/heavy.prefab");NativeApi(root);
        auto game=std::make_unique<PrefabGame>();game->SetProjectRoot(root);game->SetProjectFile(manifest);game->SetProjectAssetRoot(root/"Assets");if(!game->ConfigureStartupScene(root/"entry.scene",error))throw std::runtime_error(error);game->Run();game->VerifyAfterRun();Need(scene==Read(root/"entry.scene") && prefab==Read(root/L"Assets/生成.prefab") && heavy==Read(root/"Assets/heavy.prefab"),"SDK Prefab modified source files");std::cout<<"PASS: SDK-only Prefab native APIs/work budget/Lua rendered Model+Animator/Physics/separate Audio/fault isolation/Destroy/cleanup/source bytes\n";return 0;
    }catch(const Engine::Base::RunFailure& failure){Engine::Base::ReportRunFailure(failure);return 1;}catch(...){Engine::Base::ReportRunException("SDK Prefab",std::current_exception());return 1;}
}
