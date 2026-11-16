#include "engine/scene/prefab/RuntimePrefab.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){using namespace Engine;using namespace Engine::Scene;try{
    EntityWorld source;const auto childFirst=source.CreateEntity("Child"),root=source.CreateEntity("Root");
    source.Find(childFirst)->parent=root;source.Find(root)->transform.position={1,2,3};
    Need(source.SetModel(root,"Assets/shared.obj",std::string(32,'a')),"Model setup failed");
    source.Find(root)->hasScript=true;source.Find(root)->script.assetPath="Assets/behavior.lua";
    source.Find(root)->hasAnimator=true;source.Find(root)->animator.clip="default";
    source.Find(childFirst)->hasCamera=true;source.Find(childFirst)->camera.orthographic=true;
    source.Find(childFirst)->hasRigidBody=true;source.Find(childFirst)->rigidBody.velocity={3,4,5};
    source.Find(childFirst)->hasCollider=true;
    Prefab::PrefabValue definition;std::string error;Need(Prefab::CapturePrefabSubtree(source,root,definition,error),error.c_str());
    const auto definitionBytes=definition.dump();EntityWorld original;const auto parent=original.CreateEntity("Parent");original.Find(parent)->transform.position={10,0,0};
    auto world=original;EntityId created=777;Need(Prefab::InstantiateRuntimePrefab(world,definition,parent,created,error),error.c_str());
    Need(created!=parent && world.Find(created)->parent==parent && world.GetEntities().size()==3,"Fresh IDs/parent/count wrong");
    const auto* newRoot=world.Find(created);Need(newRoot->name=="Root" && newRoot->model.assetGuid==std::string(32,'a') && newRoot->hasAnimator && newRoot->hasScript && !newRoot->script.started && newRoot->prefabLink.is_null() && newRoot->transform.position.x==1,"Baked root/components/references changed");
    const EntityRecord* newChild=nullptr;for(const auto& record:world.GetEntities())if(record.parent==created)newChild=&record;
    Need(newChild && newChild->id!=created && newChild->hasCamera && newChild->camera.orthographic && newChild->hasRigidBody && newChild->rigidBody.velocity.y==4 && !newChild->rigidBody.grounded && newChild->prefabLink.is_null(),"Child mapping/components/transient reset wrong");
    const auto first=created;Need(Prefab::InstantiateRuntimePrefab(world,definition,0,created,error) && created!=first && world.GetEntities().size()==5 && world.Find(created)->parent==0,"Repeated instance did not allocate independently");
    Need(definition.dump()==definitionBytes,"Factory modified source definition");
    const auto refused=[&](const Prefab::PrefabValue& value,EntityId target,size_t budget){auto output=original;const auto high=output.AllocationHighWatermark();const auto bytes=output.Capture().dump();EntityId result=999;
        Need(!Prefab::InstantiateRuntimePrefab(output,value,target,result,error,budget) && result==999 && output.Capture().dump()==bytes && output.AllocationHighWatermark()==high,"Refused factory changed output or ID high-water");};
    refused(definition,999,64);refused(definition,parent,1);refused(definition,parent,0);refused(definition,parent,10001);
    auto malformed=definition;malformed["unknown"]=true;refused(malformed,parent,64);
    malformed=definition;malformed["world"]["entities"][0]["prefabLink"]=crude_json::object{};refused(malformed,parent,64);
    auto invalid=original;invalid.GetEntities().push_back(*invalid.Find(parent));const auto invalidBytes=invalid.Capture().dump();created=444;
    Need(!Prefab::InstantiateRuntimePrefab(invalid,definition,parent,created,error) && created==444 && invalid.Capture().dump()==invalidBytes,"Invalid destination World admitted");
    EntityWorld exhausted;EntityRecord last;last.id=EntityWorld::MaximumEntityId;last.name="Exhausted";exhausted.GetEntities().push_back(last);const auto exhaustedBytes=exhausted.Capture().dump();
    Need(!Prefab::InstantiateRuntimePrefab(exhausted,definition,0,created,error) && exhausted.Capture().dump()==exhaustedBytes && created==444,"Exhausted IDs changed destination/output");
    auto counted=original;allocations=0;counting=true;const bool success=Prefab::InstantiateRuntimePrefab(counted,definition,parent,created,error);counting=false;Need(success,error.c_str());const auto positions=allocations;
    const auto originalBytes=original.Capture().dump();const auto high=original.AllocationHighWatermark();
    for(size_t i=0;i<positions;++i){auto output=original;EntityId result=999;failAfter=static_cast<long>(i);const bool ok=Prefab::InstantiateRuntimePrefab(output,definition,parent,result,error);failAfter=-1;
        Need(!ok && result==999 && output.Capture().dump()==originalBytes && output.AllocationHighWatermark()==high && definition.dump()==definitionBytes,"Factory allocation failure partially published World/IDs/source");}
    std::cout<<"PASS: runtime-only baked Prefab factory, unordered subtree/fresh IDs/local TRS/components/parent/multiple instances, limits/invalid links+World/exhaustion, allocation isolation positions="<<positions<<'\n';return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
