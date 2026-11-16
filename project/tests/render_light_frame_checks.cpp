#include "engine/scene/entity/RenderLightFrame.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <new>
#include <limits>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t n){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static bool Same(const Engine::Scene::RenderLightFrame& a,const Engine::Scene::RenderLightFrame& b){return !std::memcmp(&a.gpu,&b.gpu,sizeof(a.gpu)) && a.entities==b.entities && a.skippedInvalidFrames==b.skippedInvalidFrames && a.work==b.work;}
int main(){using namespace Engine::Scene;try{
    EntityWorld world;const auto parent=world.CreateEntity("Parent");const auto a=world.CreateEntity("Directional",parent),b=world.CreateEntity("Point"),c=world.CreateEntity("Spot",parent);
    world.Find(parent)->transform.position={4,5,6};world.Find(parent)->transform.rotation.y=0.4f;world.Find(parent)->transform.scale={-2,3,4};
    LightComponent light;Need(world.SetLight(a,light),"Directional setup");light.kind=LightKind::Point;light.color={0,1,0};Need(world.SetLight(b,light),"Point setup");
    light.kind=LightKind::Spot;light.color={0,0,1};light.innerAngle=light.outerAngle;Need(world.SetLight(c,light),"Spot setup");
    const auto source=world.Capture().dump();const auto high=world.AllocationHighWatermark();RenderLightFrame frame;
    Need(BuildRenderLightFrame(world,frame)==RenderLightResult::Success && frame.gpu.mode==1 && frame.gpu.count==3 && frame.gpu.IsValid(),"Frame build failed");
    Need(frame.entities[0]==a && frame.entities[1]==b && frame.entities[2]==c && frame.gpu.lights[0].parameters.z==0 && frame.gpu.lights[1].parameters.z==1 && frame.gpu.lights[2].parameters.z==2,"Frame order/types wrong");
    Matrix4x4 matrix{};Need(world.GetWorldMatrix(c,matrix),"World matrix unavailable");const auto& gpu=frame.gpu.lights[2];
    const double len=std::sqrt(double(matrix.m[2][0])*matrix.m[2][0]+double(matrix.m[2][1])*matrix.m[2][1]+double(matrix.m[2][2])*matrix.m[2][2]);
    Need(gpu.positionRange.x==matrix.m[3][0] && gpu.positionRange.y==matrix.m[3][1] && gpu.positionRange.z==matrix.m[3][2] && std::abs(gpu.directionOuter.x-float(matrix.m[2][0]/len))<1e-6 && gpu.positionRange.w==light.range,"Parent affine pose or world-unit range wrong");
    auto reversed=world;std::reverse(reversed.GetEntities().begin(),reversed.GetEntities().end());RenderLightFrame sorted;
    Need(BuildRenderLightFrame(reversed,sorted)==RenderLightResult::Success && !std::memcmp(&sorted.gpu,&frame.gpu,sizeof(frame.gpu)) && sorted.entities==frame.entities,"Storage order changed GPU list");
    auto inactive=world;inactive.Find(parent)->active=false;Need(BuildRenderLightFrame(inactive,sorted)==RenderLightResult::Success && sorted.gpu.count==1 && sorted.entities[0]==b && !sorted.skippedInvalidFrames,"Inactive ancestor Light participated");
    for(int mode=0;mode<4;++mode){auto broken=world;if(mode==0)broken.Find(parent)->parent=c;if(mode==1)broken.Find(parent)->parent=999;if(mode==2)broken.Find(parent)->transform.scale.y=0;if(mode==3)broken.Find(parent)->transform.position.x=std::numeric_limits<float>::infinity();
        Need(BuildRenderLightFrame(broken,sorted)==RenderLightResult::Success && sorted.gpu.count==1 && sorted.entities[0]==b && sorted.skippedInvalidFrames==2,"Broken/cyclic/singular/nonfinite branch participated");}
    const auto refused=[&](const EntityWorld& input,RenderLightResult expected){auto output=frame;Need(BuildRenderLightFrame(input,output)==expected && Same(output,frame),"Refused frame published partial output");};
    auto bad=world;bad.Find(a)->light.intensity=-1;refused(bad,RenderLightResult::InvalidComponent);
    bad=world;bad.GetEntities().push_back(*bad.Find(a));refused(bad,RenderLightResult::InvalidWorld);
    bad=world;bad.Find(a)->id=0;refused(bad,RenderLightResult::InvalidWorld);
    EntityWorld capacity;for(size_t i=0;i<64;++i){const auto id=capacity.CreateEntity("Light");Need(capacity.SetLight(id,{}),"Capacity setup");}
    Need(BuildRenderLightFrame(capacity,sorted)==RenderLightResult::Success && sorted.gpu.count==64,"64 lights refused");
    capacity.SetLight(capacity.CreateEntity("65"),{});refused(capacity,RenderLightResult::Limit);
    EntityWorld large;large.GetEntities().resize(10001);refused(large,RenderLightResult::Limit);
    large.GetEntities().resize(10000);for(size_t i=0;i<10000;++i){auto& e=large.GetEntities()[i];e.id=i+1;e.parent=i?i:10000;e.hasLight=true;}
    refused(large,RenderLightResult::Limit);
    allocations=0;counting=true;RenderLightFrame counted;const auto result=BuildRenderLightFrame(world,counted);counting=false;Need(result==RenderLightResult::Success,"Counted frame failed");const auto positions=allocations;
    for(size_t i=0;i<positions;++i){auto output=frame;failAfter=static_cast<long>(i);const auto rejected=BuildRenderLightFrame(world,output);failAfter=-1;Need(rejected==RenderLightResult::AllocationFailure && Same(output,frame),"Allocation failure replaced GPU frame");}
    Need(world.Capture().dump()==source && world.AllocationHighWatermark()==high,"Light frame mutated World");
    std::cout<<"PASS: 3-kind GPU layout/world pose, deterministic ID order/mirror/active ancestry, invalid branch exclusion, 64-light/entity/work limits, output/source retention and allocation positions="<<positions<<'\n';return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
