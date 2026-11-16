#include "runtime_prefab_host_fixture.h"
#include "animator_host_fixture.h"
#include "wave_fixture.h"
#include "engine/scene/prefab/PrefabDocument.h"
#include "engine/audio/EntityAudioHost.h"
#include "engine/audio/ScriptAudioHost.h"
#include "engine/base/AssetPath.h"
#include <fstream>
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Prefab host fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Prefab host source read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
RuntimePrefabHostFixture::RuntimePrefabHostFixture()=default;
RuntimePrefabHostFixture::~RuntimePrefabHostFixture()=default;
void RuntimePrefabHostFixture::Prepare(const char* role){
    using namespace Engine;scaffold_=std::make_unique<AnimatorHostFixture>();scaffold_->Prepare(role);root_=scaffold_->root;
    Write(root_/L"Assets/生成音.wav",WaveFixture::Pcm());
    Write(root_/L"Assets/生成更新.lua",R"(local n=0
function on_create(e)
e:set_scale(1,1,1.25);assert(audio.available());audio.play('Assets/生成音.wav',true,0)
assert(scene.remove_rigid_body(e:id()) and scene.rigid_body(e:id())==nil)
assert(scene.set_rigid_body(e:id(),2,0,0,3,true,false));e:set_velocity(1.5,0,0)
assert(scene.set_collider(e:id(),0.25,0.375,0.5,true,0.25,0.75))
assert(scene.remove_collider(5) and scene.collider(5)==nil)
assert(scene.set_collider(5,0.125,0.125,0.125,true,0,0))
local x,y,z=scene.world_position(e:id())
assert(scene.raycast(x-2,y,z,1,0,0,4)==e:id())
local hits=scene.overlap_box(x,y,z,0,0,0);assert(#hits==1 and hits[1]==e:id())
end
function on_update(e,dt) n=n+1;e:set_rotation(0,n/100,0);local x,y,z=scene.world_position(e:id());assert(scene.raycast(x-2,y,z,1,0,0,4)==e:id()) end
function on_fixed_update(e,dt) e:translate(0,0,0.001);local x,y,z=scene.world_position(e:id());assert(scene.overlap_box(x,y,z,0,0,0)[1]==e:id()) end
function on_destroy(e) e:set_rotation(0,0.75,0) end
)");
    Write(root_/"Assets/prefab-driver.lua",R"(function on_create(e)
local id=assert(scene.spawn_prefab('Assets/生成.prefab',e:id()));assert(scene.parent(id)==e:id())
end
function on_update(e,dt) end
)");
    Write(root_/"Assets/prefab-fault.lua",R"(function on_update(e,dt)
e:translate(7,0,0);audio.stop_all()
assert(scene.set_collider(e:id(),2,2,2,false,0.5,0.5));assert(scene.set_rigid_body(e:id(),9,0,0,2,true,false))
assert(#scene.overlap_box(0,0,0,100,100,100)>0)
for i=1,4 do assert(scene.spawn_prefab('Assets/heavy.prefab')) end
end
)");
    Scene::EntityWorld source;const auto model=source.CreateEntity("Runtime prefab model"),child=source.CreateEntity("Runtime prefab child",model);
    Need(source.SetModel(model,"Assets/models/walk.gltf") && source.SetScript(model,"Assets/生成更新.lua"),"Prefab host components failed");
    auto* record=source.Find(model);record->transform.position={0,3,0};record->hasAnimator=true;record->animator.playOnAwake=true;
    record->hasCollider=record->hasRigidBody=true;record->rigidBody.useGravity=false;record->rigidBody.velocity={1,0,0};
    record->hasAudio=true;record->audio.assetPath="Assets/生成音.wav";record->audio.loop=record->audio.playOnAwake=true;record->audio.volume=0;
    source.Find(child)->transform.position={0,1,0};source.Find(child)->hasCollider=true;source.Find(child)->collider.halfExtents={0.1f,0.1f,0.1f};
    Prefab::PrefabValue definition;std::string error;Need(Prefab::CapturePrefabSubtree(source,model,definition,error),error.c_str());prefabBytes_=definition.dump();Write(root_/L"Assets/生成.prefab",prefabBytes_);
    Need(prefabBytes_.size()<2*1024*1024,"Heavy Prefab fixture too large");heavyPrefabBytes_=prefabBytes_+std::string(2*1024*1024-prefabBytes_.size(),' ');Write(root_/"Assets/heavy.prefab",heavyPrefabBytes_);
    Assets::AssetRegistry identities;Need(identities.Scan(root_/"Assets",error),error.c_str());
    Scene::EntityWorld world;const auto driver=world.CreateEntity("Prefab driver"),fault=world.CreateEntity("Prefab fault"),camera=world.CreateEntity("Camera");
    Need(driver==Driver && fault==Fault && camera==3 && world.SetScript(driver,"Assets/prefab-driver.lua") && world.SetScript(fault,"Assets/prefab-fault.lua"),"Prefab host startup IDs failed");
    world.Find(camera)->hasCamera=true;world.Find(camera)->transform.position={0,3,-8};Need(world.Save(root_/"entry.scene",error),error.c_str());baseline_=world.Capture().dump();sceneBytes_=Read(root_/"entry.scene");
    std::cout<<"RUNTIME_PREFAB_HOST_FIXTURE="<<Base::PathToUtf8(root_)<<'\n';
}
void RuntimePrefabHostFixture::CaptureGpu(ID3D12Device* device){scaffold_->CaptureGpu(device);}
void RuntimePrefabHostFixture::VerifyGpu() const{scaffold_->VerifyGpu();VerifySources();}
void RuntimePrefabHostFixture::VerifySources() const{Need(Read(root_/"entry.scene")==sceneBytes_ && Read(root_/L"Assets/生成.prefab")==prefabBytes_ && Read(root_/"Assets/heavy.prefab")==heavyPrefabBytes_,"Prefab runtime changed startup Scene/source bytes");}
void RuntimePrefabHostFixture::Observe(const Engine::Scene::EntityWorld& world,const Engine::AudioSystem::EntityAudioHost& audio,
    const Engine::AudioSystem::ScriptAudioHost& scriptAudio,const std::string& driverError,const std::string& faultError,const std::string& rootError){
    using namespace Engine;++frames_;Need(driverError.empty(),driverError.c_str());const auto* record=world.Find(SpawnedRoot);if(!record){Need(frames_<5,"Actual Prefab did not spawn");return;}
    Need(world.GetEntities().size()==5 && world.Find(5) && world.Find(5)->parent==SpawnedRoot && record->parent==Driver && record->prefabLink.is_null(),"Actual Prefab topology/rollback failed");
    Need(faultError.find("Prefab admission 8 MiB")!=std::string::npos && world.Find(Fault)->transform.position.x==0,"Faulty Prefab peer did not hit native work budget or leaked prefix");
    Need(!world.Find(Fault)->hasCollider && !world.Find(Fault)->hasRigidBody,"Faulty Prefab peer published Physics prefix");
    Need(record->hasModel && record->hasAnimator && record->hasScript && record->hasAudio && record->hasRigidBody && record->hasCollider,"Actual Prefab lost components");if(frames_<5)return;
    Need(rootError.empty(),rootError.c_str());Need(record->script.started && record->transform.scale.z==1.25f && record->transform.rotation.y>0 && record->transform.position.z>0,"New Prefab Script Create/Update/Fixed did not execute");
    Need(record->transform.position.x>0 && record->rigidBody.velocity.x>0 && !record->rigidBody.grounded,"Prefab Physics did not advance");
    Need(record->rigidBody.mass==3 && record->rigidBody.velocity.x==1.5f && !record->rigidBody.useGravity && record->collider.halfExtents.y==0.375f && record->collider.isTrigger && record->collider.friction==0.25f && record->collider.restitution==0.75f && world.Find(5)->collider.halfExtents.x==0.125f && world.Find(5)->collider.isTrigger,"Actual Lua Physics replacement/removal/re-add not consumed");
    const auto* scene=dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene());Need(scene && scene->GetModelRenderStatus(SpawnedRoot).loaded,"Prefab Model owner not loaded");
    const auto animator=scene->GetAnimatorRenderStatus(SpawnedRoot);Need(animator.ready && animator.playing && animator.time>0,"Prefab Animator owner did not advance");
    Need(audio.SourceCount()==1 && audio.VoiceCount()==1 && audio.BufferBytes()==4 && audio.GetLastError(SpawnedRoot).empty() && audio.QueuedVoicesHaveVolume(SpawnedRoot,0),"Prefab component Audio owner missing");
    Need(scriptAudio.OwnerCount()==1 && scriptAudio.VoiceCount()==1 && scriptAudio.BufferBytes()==4,"New Prefab Lua Audio owner missing");progressed_=true;
}
