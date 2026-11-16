#include "RenderParticleFrame.h"
#include "EntityTransformMatrix.h"
#include <algorithm>
#include <unordered_map>
namespace Engine::Scene {
RenderParticleResult BuildRenderParticleFrame(const EntityWorld& world,RenderParticleFrame& output) noexcept {
    try {
        const auto& entities=world.GetEntities();if(entities.size()>10000)return RenderParticleResult::Limit;
        std::unordered_map<EntityId,size_t> indices;indices.reserve(entities.size());
        for(size_t i=0;i<entities.size();++i){const auto& entity=entities[i];if(!entity.id||entity.id>EntityWorld::MaximumEntityId||!indices.emplace(entity.id,i).second)return RenderParticleResult::InvalidWorld;if(entity.hasParticle&&!entity.particle.IsValid())return RenderParticleResult::InvalidComponent;}
        RenderParticleFrame candidate;std::vector<uint32_t> visited(entities.size(),0);std::vector<size_t> chain;uint32_t stamp=0;
        const auto finite=[](const Matrix4x4& matrix){for(const auto& row:matrix.m)for(float n:row)if(!std::isfinite(n))return false;return matrix.m[0][3]==0&&matrix.m[1][3]==0&&matrix.m[2][3]==0&&matrix.m[3][3]==1;};
        for(size_t i=0;i<entities.size();++i){
            const auto& entity=entities[i];if(!entity.hasParticle||!entity.particle.enabled||(entity.particle.assetPath.empty()&&entity.particle.assetGuid.empty()))continue;
            chain.clear();++stamp;size_t cursor=i;bool inactive=false,broken=false;
            for(;;){if(++candidate.work>262144)return RenderParticleResult::Limit;if(visited[cursor]==stamp){broken=true;break;}visited[cursor]=stamp;if(!entities[cursor].active){inactive=true;break;}chain.push_back(cursor);if(!entities[cursor].parent)break;const auto parent=indices.find(entities[cursor].parent);if(parent==indices.end()){broken=true;break;}cursor=parent->second;}
            if(inactive)continue;Matrix4x4 model{};
            if(!broken)for(size_t j=0;j<chain.size();++j){if(++candidate.work>262144)return RenderParticleResult::Limit;const auto local=EntityLocalMatrix(entities[chain[j]].transform);if(!finite(local)){broken=true;break;}model=j?model*local:local;}
            if(broken||!finite(model)){++candidate.skippedInvalidFrames;continue;}
            if(candidate.items.size()==RenderParticleFrame::MaximumEmitters||entity.particle.capacity>RenderParticleFrame::MaximumCapacity-candidate.capacity)return RenderParticleResult::Limit;
            candidate.items.push_back({entity.id,entity.particle,model});candidate.capacity+=entity.particle.capacity;
        }
        std::sort(candidate.items.begin(),candidate.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});output=std::move(candidate);return RenderParticleResult::Success;
    }catch(const std::bad_alloc&){return RenderParticleResult::AllocationFailure;}catch(...){return RenderParticleResult::InvalidWorld;}
}
}
