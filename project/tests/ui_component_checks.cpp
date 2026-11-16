#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/editor/PrefabInstance.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include "engine/scene/entity/WorldPlayback.h"
#include <iostream>
#include <new>
#include <cstdlib>
#include <limits>

static long failAfter=-1;
static bool counting=false;
static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){using namespace Engine;using namespace Scene;using namespace Editor;try{
    EntityWorld world;const auto root=world.CreateEntity("Canvas"),child=world.CreateEntity("Sprite",root);
    const auto legacy=world.Capture();std::string error;
    Need(!legacy["entities"][0].contains("canvas") && !legacy["entities"][1].contains("sprite"),"Legacy Scene gained UI fields");
    CanvasComponent canvas;SpriteComponent sprite;
    Need(canvas.IsValid() && sprite.IsValid() && DecodeCanvas(crude_json::object{},canvas) && DecodeSprite(crude_json::object{},sprite),"UI defaults/draft invalid");
    {auto out=canvas;Need(DecodeCanvas(EncodeCanvas(canvas),out) && out.scaleMode==CanvasScaleMode::ScaleWithOutput,"ScaleWithOutput roundtrip failed");
        auto draft=sprite;Need(DecodeSprite(EncodeSprite(sprite),draft) && draft.assetPath.empty() && draft.assetGuid.empty(),"Unassigned draft roundtrip failed");}
    canvas.enabled=false;canvas.scaleMode=CanvasScaleMode::ConstantPixels;canvas.referenceSize={1920,1080};canvas.sortOrder=-32768;
    sprite.assetPath="Assets/Images/日本語の長い画像ファイル.png";sprite.assetGuid=std::string(32,'a');sprite.size={0,16384};sprite.pivot={-16,16};sprite.color={0.2f,0.3f,0.4f,0.5f};sprite.flipX=true;sprite.flipY=true;sprite.sortOrder=32767;
    Need(world.SetCanvas(root,canvas) && world.SetSprite(child,sprite),"UI setter failed");
    const auto authored=world.Capture();const auto bytes=authored.dump();const auto high=world.AllocationHighWatermark();
    EntityWorld restored;Need(restored.Restore(authored,error) && restored.Capture().dump()==bytes,"UI roundtrip lost fields");
    Need(restored.Restore(SceneValue::parse(bytes),error) && restored.Capture().dump()==bytes,"Serialized JSON roundtrip lost UI");
    const auto reject=[&](const SceneValue& v,bool isCanvas){
        if(isCanvas){auto out=canvas;Need(!DecodeCanvas(v,out) && EncodeCanvas(out).dump()==EncodeCanvas(canvas).dump(),"Canvas decode changed output");}
        else{auto out=sprite;Need(!DecodeSprite(v,out) && EncodeSprite(out).dump()==EncodeSprite(sprite).dump(),"Sprite decode changed output");}
        auto document=authored;document["entities"][isCanvas?0:1][isCanvas?"canvas":"sprite"]=v;
        Need(!world.Restore(document,error) && !error.empty() && world.Capture().dump()==bytes && world.AllocationHighWatermark()==high,"Invalid UI published World/IDs");
    };
    for(bool c:{false,true}){
        reject(nullptr,c);reject(true,c);reject(crude_json::array{},c);
        auto v=c?EncodeCanvas(canvas):EncodeSprite(sprite);v["unknown"]=true;reject(v,c);
        for(double order:{0.5,-32769.0,32768.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){v=c?EncodeCanvas(canvas):EncodeSprite(sprite);v["sortOrder"]=order;reject(v,c);}
        v=c?EncodeCanvas(canvas):EncodeSprite(sprite);v["enabled"]=1.0;reject(v,c);v["sortOrder"]="0";reject(v,c);
    }
    for(const auto* mode:{"invalid","","ConstantPixels"}){auto v=EncodeCanvas(canvas);v["scaleMode"]=mode;reject(v,true);}
    {auto v=EncodeCanvas(canvas);v["scaleMode"]=true;reject(v,true);}
    for(const auto* field:{"referenceSize","size","pivot"}){
        const bool c=std::string_view(field)=="referenceSize";
        auto v=c?EncodeCanvas(canvas):EncodeSprite(sprite);v[field]=crude_json::array{1.0};reject(v,c);
        v[field]=crude_json::array{1.0,true};reject(v,c);v[field]=crude_json::array{1.0,std::numeric_limits<double>::infinity()};reject(v,c);
        v[field]=crude_json::array{1.0,16385.0};reject(v,c);
        v[field]=crude_json::array{1.0,c?0.0:(std::string_view(field)=="size"?-1e-100:-17.0)};reject(v,c);
    }
    for(const auto& path:std::initializer_list<std::string>{"/root.png","C:/root.png","../root.png","Assets/../root.png","Assets//a.png","Assets/./a.png","Assets/a.png/","Assets/a.png.","Assets/a.png ","Assets/a?.png","Assets\\a.png","Assets/CON.png","Assets/lpt9.png",std::string("Assets/a\0b.png",14),std::string("Assets/\xc0\xaf.png"),std::string(4097,'a')}){
        auto v=EncodeSprite(sprite);v["asset"]=path;reject(v,false);
    }
    for(const auto* guid:{"A234567890abcdef","abc","../../escape"}){auto v=EncodeSprite(sprite);v["guid"]=guid;reject(v,false);}
    for(const auto* field:{"asset","guid","flipX","flipY"}){auto v=EncodeSprite(sprite);v[field]=1.0;reject(v,false);}
    {auto v=EncodeSprite(sprite);v["color"]=crude_json::array{1.0,1.0,1.0};reject(v,false);v["color"]=crude_json::array{1.0,1.0,1.0,-1e-100};reject(v,false);}
    {auto bad=canvas;bad.scaleMode=static_cast<CanvasScaleMode>(99);Need(!world.SetCanvas(root,bad),"Native invalid scale mode accepted");auto out=canvas;Need(!DecodeCanvas(EncodeCanvas(bad),out),"Invalid mode serialized as valid");}
    {auto bad=sprite;bad.size.x=-1;Need(!world.SetSprite(child,bad),"Native invalid Sprite accepted");}
    Need(!world.SetCanvas(999,canvas) && !world.SetSprite(999,sprite) && world.Capture().dump()==bytes,"Invalid setters changed Scene");
    Need(world.SetSprite(child,world.Find(child)->sprite) && world.Capture().dump()==bytes,"Self-alias setter corrupted strings");
    // Each owning-string copy must fail without publishing either string/flag.
    SpriteComponent replacement=sprite;replacement.assetPath="Assets/Images/another-long-image-name-for-allocation.png";replacement.assetGuid=std::string(32,'b');
    allocations=0;counting=true;const bool set=world.SetSprite(child,replacement);counting=false;const auto setterPositions=allocations;
    Need(set && setterPositions>=2,"Setter string allocation probe missing");Need(world.Restore(authored,error),error.c_str());
    for(size_t fault=0;fault<setterPositions;++fault){failAfter=static_cast<long>(fault);const bool ok=world.SetSprite(child,replacement);failAfter=-1;Need(!ok && world.Capture().dump()==bytes && world.AllocationHighWatermark()==high,"Partial Sprite setter publication");}
    for(size_t fault=0;fault<setterPositions;++fault){auto absent=world;absent.RemoveSprite(child);const auto previous=absent.Capture().dump();failAfter=static_cast<long>(fault);const bool ok=absent.SetSprite(child,replacement);failAfter=-1;Need(!ok && !absent.Find(child)->hasSprite && absent.Capture().dump()==previous,"Failed setter enabled absent Sprite");}
    auto encoded=EncodeSprite(replacement);SpriteComponent decoded;
    allocations=0;counting=true;const bool decode=DecodeSprite(encoded,decoded);counting=false;const auto decodePositions=allocations;Need(decode && decodePositions>=2,"Decode string allocation probe missing");
    for(size_t fault=0;fault<decodePositions;++fault){auto output=sprite;failAfter=static_cast<long>(fault);const bool ok=DecodeSprite(encoded,output);failAfter=-1;Need(!ok && EncodeSprite(output).dump()==EncodeSprite(sprite).dump(),"Decode allocation failure published partial Sprite");}
    // Clone and linked/runtime Prefab use the same authored schema.
    auto duplicated=world;EntityId clone=0;Need(duplicated.DuplicateSubtree(root,0,clone,error),error.c_str());
    Need(duplicated.Find(clone)->hasCanvas,"Clone lost Canvas");bool clonedSprite=false;for(const auto& e:duplicated.GetEntities())if(e.parent==clone)clonedSprite=e.hasSprite && EncodeSprite(e.sprite).dump()==EncodeSprite(sprite).dump();Need(clonedSprite,"Clone lost Sprite");
    PrefabValue definition;Need(CapturePrefabSubtree(world,root,definition,error),error.c_str());
    EntityWorld linked;EntitySelection selected;EntityHistory history;EntityId instance=0;
    Need(InstantiatePrefab(linked,definition,std::string(32,'c'),0,selected,history,instance,error),error.c_str());
    Need(linked.Find(instance)->hasCanvas,"Linked Prefab lost Canvas");
    EntityWorld runtime;EntityId runtimeRoot=0;Need(Prefab::InstantiateRuntimePrefab(runtime,definition,0,runtimeRoot,error),error.c_str());
    bool runtimeSprite=false;for(const auto& e:runtime.GetEntities())if(e.parent==runtimeRoot)runtimeSprite=e.hasSprite && EncodeSprite(e.sprite).dump()==EncodeSprite(sprite).dump();Need(runtimeSprite && runtime.Find(runtimeRoot)->hasCanvas,"Runtime Prefab lost UI");
    EntityHistory edits;const auto before=world.Capture();world.RemoveSprite(child);world.RemoveCanvas(root);
    Need(world.Capture().dump()==legacy.dump() && edits.Record(before,world,root,root,error),"Removal changed legacy shape/history failed");edits.Commit();EntityId selection=root;
    Need(edits.Undo(world,selection,error) && world.Capture().dump()==bytes,"UI Undo failed");
    Need(edits.Redo(world,selection,error) && world.Capture().dump()==legacy.dump(),"UI Redo failed");Need(edits.Undo(world,selection,error),error.c_str());
    WorldPlayback playback;Scripting::ScriptRuntime scripts;Physics::PhysicsWorld physics;Need(playback.Play(world),"Play refused");world.RemoveSprite(child);world.RemoveCanvas(root);
    Need(playback.Stop(world,scripts,physics,error) && world.Capture().dump()==bytes,"Stop lost authored UI");
    EntityWorld counted;allocations=0;counting=true;const bool restore=counted.Restore(authored,error);counting=false;const auto positions=allocations;Need(restore,"Restore allocation probe failed");
    EntityWorld original;original.CreateEntity("Existing");const auto originalBytes=original.Capture().dump();const auto originalHigh=original.AllocationHighWatermark();
    for(size_t fault=0;fault<positions;++fault){auto output=original;failAfter=static_cast<long>(fault);const bool ok=output.Restore(authored,error);failAfter=-1;Need(!ok && output.Capture().dump()==originalBytes && output.AllocationHighWatermark()==originalHigh,"Restore allocation failure published UI/IDs");}
    std::cout<<"PASS: Canvas/Sprite authored defaults, strict codecs, legacy omission, clone/Prefab/history/Play Stop, string setter allocation isolation="<<setterPositions<<", decode allocation isolation="<<decodePositions<<", Restore allocation isolation="<<positions<<" (not UI rendering/layout/asset resolution)\n";return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
