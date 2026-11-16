#include "engine/scene/entity/RenderSpriteFrame.h"
#include <iostream>
#include <cstdlib>
#include <new>
#include <limits>
#include <cstring>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t n){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static bool MatrixSame(const Matrix4x4& a,const Matrix4x4& b){for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(std::abs(a.m[r][c]-b.m[r][c])>1e-4f)return false;return true;}
static bool Same(const Engine::Scene::RenderSpriteFrame& a,const Engine::Scene::RenderSpriteFrame& b){
    if(a.extent!=b.extent || !MatrixSame(a.projection,b.projection) || a.work!=b.work || a.skippedInvalidFrames!=b.skippedInvalidFrames || a.withoutCanvas!=b.withoutCanvas || a.unassigned!=b.unassigned || a.items.size()!=b.items.size())return false;
    for(size_t i=0;i<a.items.size();++i){const auto& x=a.items[i];const auto& y=b.items[i];
        if(x.entity!=y.entity || x.canvas!=y.canvas || x.canvasOrder!=y.canvasOrder || x.spriteOrder!=y.spriteOrder || !MatrixSame(x.world,y.world) ||
            x.pivot.x!=y.pivot.x || x.pivot.y!=y.pivot.y || x.color.x!=y.color.x || x.color.y!=y.color.y || x.color.z!=y.color.z || x.color.w!=y.color.w || x.flipX!=y.flipX || x.flipY!=y.flipY)return false;
    }return true;
}
int main(){using namespace Engine::Scene;try{
    EntityWorld world;const auto above=world.CreateEntity("Above"),canvas=world.CreateEntity("Canvas",above),parent=world.CreateEntity("Nonuniform parent",canvas),a=world.CreateEntity("Rotated",parent),b=world.CreateEntity("Peer",canvas);
    world.Find(above)->transform.position={4,5,6};world.Find(canvas)->transform.position={20,10,0};
    world.Find(parent)->transform.scale={-2,3,1};world.Find(parent)->transform.rotation.z=0.3f;
    world.Find(a)->transform.rotation.z=0.7f;world.Find(a)->transform.position={10,20,3};
    CanvasComponent settings;settings.referenceSize={640,360};Need(world.SetCanvas(canvas,settings),"Canvas setup");
    SpriteComponent sprite;sprite.assetPath="Assets/image.png";sprite.size={20,30};sprite.pivot={0.25f,0.75f};sprite.flipX=true;sprite.color={0.2f,0.3f,0.4f,0.5f};
    Need(world.SetSprite(a,sprite),"Sprite A setup");sprite.sortOrder=-1;Need(world.SetSprite(b,sprite),"Sprite B setup");
    const auto source=world.Capture().dump();const auto high=world.AllocationHighWatermark();RenderSpriteFrame frame;
    Need(BuildRenderSpriteFrame(world,{320,180},frame)==RenderSpriteResult::Success && frame.items.size()==2,"Frame build");
    Need(frame.items[0].entity==b && frame.items[1].entity==a && frame.items[1].canvas==canvas && frame.items[1].flipX && frame.items[1].color.w==0.5f,"Frame settings/order");
    Matrix4x4 composed{};Need(world.GetWorldMatrix(a,composed),"World composition");
    const Matrix4x4 size{{{20,0,0,0},{0,30,0,0},{0,0,1,0},{0,0,0,1}}},scale{{{0.5f,0,0,0},{0,0.5f,0,0},{0,0,1,0},{0,0,0,1}}};
    Need(MatrixSame(frame.items[1].world,size*composed*scale),"Full parent shear/mirror/above-Canvas transform lost");
    Need(frame.projection.m[0][0]==2.0f/320 && frame.projection.m[1][1]==-2.0f/180 && frame.projection.m[2][2]==0 && frame.projection.m[3][0]==-1 && frame.projection.m[3][1]==1,"Output projection");
    auto reordered=world;std::reverse(reordered.GetEntities().begin(),reordered.GetEntities().end());RenderSpriteFrame other;
    Need(BuildRenderSpriteFrame(reordered,{320,180},other)==RenderSpriteResult::Success && Same(frame,other),"Storage changed deterministic list");
    auto constant=world;constant.Find(canvas)->canvas.scaleMode=CanvasScaleMode::ConstantPixels;
    Need(BuildRenderSpriteFrame(constant,{640,180},other)==RenderSpriteResult::Success && MatrixSame(other.items[1].world,size*composed),"ConstantPixels scaled");
    Need(BuildRenderSpriteFrame(world,{640,180},other)==RenderSpriteResult::Success && std::abs(other.items[1].world.m[3][0]-2*frame.items[1].world.m[3][0])<1e-4f && other.items[1].world.m[3][1]==frame.items[1].world.m[3][1],"Independent output axes");
    auto anchored=constant;
    anchored.Find(a)->sprite.anchor={1,0.5f};
    EntityWorld reloaded;
    std::string error;
    Need(reloaded.Restore(anchored.Capture(),error),"Restore anchored Sprite");
    for(const auto extent:{Engine::Base::ViewExtent{1280,720},Engine::Base::ViewExtent{960,720}}){
        auto expected=size*composed;
        expected.m[3][0]+=extent.width;
        expected.m[3][1]+=0.5f*extent.height;
        Need(BuildRenderSpriteFrame(reloaded,extent,other)==RenderSpriteResult::Success &&
            other.items.size()==2 && MatrixSame(other.items[1].world,expected),"Anchored position at alternate aspect");
        const auto& item=other.items[1];
        const auto center=item.OutputPoint({0.5f,0.5f});
        const auto left=item.OutputPoint({0,0});
        Need(std::abs(left.x-(expected.m[3][0]+0.25f*expected.m[0][0]-0.75f*expected.m[1][0]))<1e-4f &&
            item.ContainsOutputPoint(center) && !item.ContainsOutputPoint(item.OutputPoint({1.2f,0.5f})),
            "UI quad or picking disagrees with mirrored Sprite geometry");
    }
    auto nested=world;settings.sortOrder=-5;settings.scaleMode=CanvasScaleMode::ConstantPixels;Need(nested.SetCanvas(parent,settings),"Nested Canvas setup");
    Need(BuildRenderSpriteFrame(nested,{320,180},other)==RenderSpriteResult::Success && other.items[0].entity==a && other.items[0].canvas==parent && MatrixSame(other.items[0].world,size*composed),"Nearest Canvas/double scale/sort group");
    nested.Find(canvas)->canvas.enabled=false;Need(BuildRenderSpriteFrame(nested,{320,180},other)==RenderSpriteResult::Success && other.items.empty(),"Nested Canvas bypassed disabled outer Canvas");
    auto hidden=world;hidden.Find(above)->active=false;Need(BuildRenderSpriteFrame(hidden,{320,180},other)==RenderSpriteResult::Success && other.items.empty(),"Inactive ancestry");
    for(int mode=0;mode<5;++mode){auto broken=world;
        if(mode==0)broken.Find(parent)->parent=a;
        if(mode==1)broken.Find(parent)->parent=999;
        if(mode==2)broken.Find(parent)->transform.scale.x=0;
        if(mode==3)broken.Find(parent)->transform.position.x=std::numeric_limits<float>::infinity();
        if(mode==4)broken.Find(parent)->transform.scale.x=(std::numeric_limits<float>::max)();
        Need(BuildRenderSpriteFrame(broken,{320,180},other)==RenderSpriteResult::Success && other.items.size()==1 && other.items[0].entity==b && other.skippedInvalidFrames==1,"Invalid branch rendered/prefix lost peer");
    }
    auto draft=world;draft.RemoveCanvas(canvas);Need(BuildRenderSpriteFrame(draft,{320,180},other)==RenderSpriteResult::Success && other.items.empty() && other.withoutCanvas==2,"Orphan implicit Canvas");
    draft=world;draft.Find(a)->sprite.assetPath.clear();Need(BuildRenderSpriteFrame(draft,{320,180},other)==RenderSpriteResult::Success && other.items.size()==1 && other.unassigned==1,"Unassigned fallback");
    for(int mode=0;mode<3;++mode){draft=world;if(mode==0)draft.Find(a)->sprite.enabled=false;if(mode==1)draft.Find(a)->sprite.size.x=0;if(mode==2)draft.Find(a)->sprite.color.w=0;
        Need(BuildRenderSpriteFrame(draft,{320,180},other)==RenderSpriteResult::Success && other.items.size()==1,"Disabled/empty/transparent Sprite");}
    const auto reject=[&](const EntityWorld& input,RenderSpriteResult expected){auto out=frame;Need(BuildRenderSpriteFrame(input,{320,180},out)==expected && Same(out,frame),"Refusal replaced complete frame");};
    auto bad=world;bad.Find(canvas)->canvas.referenceSize.x=0;reject(bad,RenderSpriteResult::InvalidComponent);
    bad=world;bad.Find(a)->sprite.size.x=-1;reject(bad,RenderSpriteResult::InvalidComponent);
    bad=world;bad.GetEntities().push_back(*bad.Find(a));reject(bad,RenderSpriteResult::InvalidWorld);
    bad=world;bad.Find(a)->id=0;reject(bad,RenderSpriteResult::InvalidWorld);
    for(const auto extent:{Engine::Base::ViewExtent{0,180},Engine::Base::ViewExtent{4097,1},Engine::Base::ViewExtent{4096,4096}}){auto out=frame;Need(BuildRenderSpriteFrame(world,extent,out)==RenderSpriteResult::InvalidExtent && Same(out,frame),"Invalid extent published");}
    EntityWorld capacity;const auto c=capacity.CreateEntity("Canvas");capacity.SetCanvas(c,{});
    for(size_t i=0;i<1024;++i)Need(capacity.SetSprite(capacity.CreateEntity("Sprite",c),sprite),"Capacity setup");
    Need(BuildRenderSpriteFrame(capacity,{320,180},other)==RenderSpriteResult::Success && other.items.size()==1024,"1024 refused");
    capacity.SetSprite(capacity.CreateEntity("1025",c),sprite);reject(capacity,RenderSpriteResult::Limit);
    EntityWorld large;large.GetEntities().resize(10001);reject(large,RenderSpriteResult::Limit);
    large.GetEntities().resize(10000);for(size_t i=0;i<10000;++i){auto& e=large.GetEntities()[i];e.id=i+1;e.parent=i?i:10000;e.hasSprite=true;e.sprite=sprite;}reject(large,RenderSpriteResult::Limit);
    allocations=0;counting=true;RenderSpriteFrame counted;const auto success=BuildRenderSpriteFrame(world,{320,180},counted);counting=false;const auto positions=allocations;Need(success==RenderSpriteResult::Success,"Counted build");
    for(size_t fault=0;fault<positions;++fault){auto out=frame;failAfter=static_cast<long>(fault);const auto result=BuildRenderSpriteFrame(world,{320,180},out);failAfter=-1;Need(result==RenderSpriteResult::AllocationFailure && Same(out,frame),"Allocation replaced frame");}
    Need(world.Capture().dump()==source && world.AllocationHighWatermark()==high,"Builder changed World");
    std::cout<<"PASS: Canvas output axes/constant/nested/disabled, full affine/shear/mirror hierarchy, stable sort/overlay projection, invalid/orphan/unassigned exclusions, entity/work/1024 limits and allocation isolation positions="<<positions<<" (not GPU pixels)\n";return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
