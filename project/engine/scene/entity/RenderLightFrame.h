#pragma once
#include "EntityWorld.h"
#include "EntityTransformMatrix.h"
#include "../../math/data/AuthoredLightData.h"
#include <algorithm>
#include <unordered_map>
#include <new>

namespace Engine::Scene {
enum class RenderLightResult { Success, InvalidWorld, InvalidComponent, Limit, AllocationFailure };
struct RenderLightFrame {
    Math::AuthoredLightData gpu=[] {Math::AuthoredLightData value;value.mode=1;return value;}();
    std::array<EntityId,Math::AuthoredLightData::MaximumLights> entities{};
    size_t skippedInvalidFrames=0,work=0;
};
// Current-pose world units, +local Z is emitted direction. Scale affects the
// composed position/direction, not authored range. A bounded fresh scan; no BVH.
inline RenderLightResult BuildRenderLightFrame(const EntityWorld& world,RenderLightFrame& output) noexcept {
    constexpr size_t MaximumEntities=10000,MaximumWork=262144;
    try {
        const auto& entities=world.GetEntities();const auto count=entities.size();
        if(count>MaximumEntities)return RenderLightResult::Limit;
        RenderLightFrame candidate;
        std::unordered_map<EntityId,size_t> indices;indices.reserve(count);
        for(size_t i=0;i<count;++i){
            if(!entities[i].id || entities[i].id>EntityWorld::MaximumEntityId || !indices.emplace(entities[i].id,i).second)return RenderLightResult::InvalidWorld;
            if(entities[i].hasLight && !entities[i].light.IsValid())return RenderLightResult::InvalidComponent;
        }
        std::vector<size_t> chain;std::vector<uint32_t> visited(count,0);uint32_t stamp=0;
        std::array<std::pair<EntityId,Math::AuthoredLightGpu>,Math::AuthoredLightData::MaximumLights> ordered{};
        const auto frameValid=[](const Matrix4x4& m){
            double bound=0;for(const auto& row:m.m)for(float n:row)if(!std::isfinite(n))return false;
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)bound=(std::max)(bound,std::abs(double(m.m[r][c])));
            if(bound==0)return false;
            double a[3][3];for(int r=0;r<3;++r)for(int c=0;c<3;++c)a[r][c]=m.m[r][c]/bound;
            const double det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
            return std::isfinite(det) && std::abs(det)>=1e-10;
        };
        for(size_t i=0;i<count;++i){
            const auto& entity=entities[i];if(!entity.hasLight || !entity.light.enabled)continue;
            chain.clear();++stamp;auto cursor=i;bool inactive=false,invalid=false;
            for(;;){
                if(++candidate.work>MaximumWork)return RenderLightResult::Limit;
                if(visited[cursor]==stamp){invalid=true;break;}visited[cursor]=stamp;
                if(!entities[cursor].active){inactive=true;break;}
                chain.push_back(cursor);
                if(!entities[cursor].parent)break;
                const auto parent=indices.find(entities[cursor].parent);if(parent==indices.end()){invalid=true;break;}
                cursor=parent->second;
            }
            if(inactive)continue;
            Matrix4x4 matrix{};
            if(!invalid){
                for(size_t index=0;index<chain.size();++index){
                    if(++candidate.work>MaximumWork)return RenderLightResult::Limit;
                    const auto local=EntityLocalMatrix(entities[chain[index]].transform);
                    if(!frameValid(local)){invalid=true;break;}
                    matrix=index?matrix*local:local;
                }
            }
            if(invalid || !frameValid(matrix)){++candidate.skippedInvalidFrames;continue;}
            const double x=matrix.m[2][0],y=matrix.m[2][1],z=matrix.m[2][2],length=std::sqrt(x*x+y*y+z*z);
            if(!std::isfinite(length) || length==0){++candidate.skippedInvalidFrames;continue;}
            if(candidate.gpu.count==Math::AuthoredLightData::MaximumLights)return RenderLightResult::Limit;
            const auto& light=entity.light;Math::AuthoredLightGpu packed;
            packed.colorIntensity={light.color.x,light.color.y,light.color.z,light.intensity};
            packed.positionRange={matrix.m[3][0],matrix.m[3][1],matrix.m[3][2],light.range};
            packed.directionOuter={float(x/length),float(y/length),float(z/length),std::cos(light.outerAngle)};
            packed.parameters={std::cos(light.innerAngle),light.decay,float(light.kind),0};
            ordered[candidate.gpu.count++]={entity.id,packed};
        }
        std::sort(ordered.begin(),ordered.begin()+candidate.gpu.count,[](const auto& a,const auto& b){return a.first<b.first;});
        for(size_t i=0;i<candidate.gpu.count;++i){candidate.entities[i]=ordered[i].first;candidate.gpu.lights[i]=ordered[i].second;}
        if(!candidate.gpu.IsValid())return RenderLightResult::InvalidComponent;
        output=candidate;return RenderLightResult::Success;
    }catch(const std::bad_alloc&){return RenderLightResult::AllocationFailure;}
    catch(...){return RenderLightResult::InvalidWorld;}
}
}
