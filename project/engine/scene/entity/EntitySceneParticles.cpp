#include "EntityScene.h"
#include "../../particle/core/ParticleManager.h"
#include "../../assets/SpriteReference.h"
#include "../../base/AssetPath.h"
#include "CameraManager.h"
#include "Logger.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
namespace Engine::Scene {
namespace {
constexpr float Step=1.f/60;
class AuthoredParticleBehavior final:public Particle::IParticleBehavior {
    ParticleComponent settings_;std::mt19937 random_;
    float Random(float center,float spread){return center+spread*float(double(random_())/double((std::mt19937::max)())*2-1);}
public:
    explicit AuthoredParticleBehavior(const ParticleComponent& settings):settings_(settings),random_(settings.seed){}
    Particle::Particle Create(std::mt19937&,const Vector3& position) override {
        Particle::Particle value{};value.transform.translate=position;value.transform.scale={settings_.size,settings_.size,settings_.size};value.lifetime=settings_.lifetime;value.color=settings_.startColor;
        value.Velocity={Random(settings_.velocity.x,settings_.spread.x),Random(settings_.velocity.y,settings_.spread.y),Random(settings_.velocity.z,settings_.spread.z)};return value;
    }
    void Update(Particle::Particle& value,float dt,Math::Material*) override {
        value.currentTime=(std::min)(value.lifetime,value.currentTime+dt);value.transform.translate.x+=value.Velocity.x*dt;value.transform.translate.y+=value.Velocity.y*dt;value.transform.translate.z+=value.Velocity.z*dt;
        const auto age=value.currentTime/value.lifetime;
        const auto size=settings_.SizeAt(age);
        value.transform.scale={size,size,size};
        value.color=settings_.ColorAt(age);
        if(value.currentTime>=value.lifetime)value.color.w=0;
    }
};
Particle::VerticesType Geometry(ParticleGeometry value){return value==ParticleGeometry::Quad?Particle::VerticesType::Quad:value==ParticleGeometry::Ring?Particle::VerticesType::Ring:Particle::VerticesType::Cylinder;}
std::filesystem::path ResolveParticle(const ParticleComponent& value,const Assets::AssetRegistry& assets,const std::filesystem::path& root,std::string& error){SpriteComponent image;image.assetPath=value.assetPath;image.assetGuid=value.assetGuid;return Assets::ResolveSpriteReference(image,assets,root,error);}
}
struct EntityScene::ParticleSceneState {
    struct Entry {std::string signature,name;Particle::ParticleGroupOwner owner;double remainder=0;uint32_t burst=0;};
    std::unordered_map<EntityId,Entry> entries;RenderParticleFrame frame;ParticleRenderStatus status;double accumulator=0;uint64_t generation=0,scope=0;bool drawFailure=false;
    EntityId preview=0;
    bool eventPreview=false;
    ParticleSceneState(){static std::atomic<uint64_t> next{1};auto value=next.load();for(;;){if(value==(std::numeric_limits<uint64_t>::max)())throw std::overflow_error("Particle Scene scope exhausted");if(next.compare_exchange_weak(value,value+1)){scope=value;break;}}}
};
void EntityScene::ParticleStateDeleter::operator()(ParticleSceneState* state) const noexcept {delete state;}
void EntityScene::FinalizeParticles() noexcept {particles_.reset();}
EntityScene::ParticleRenderStatus EntityScene::GetParticleRenderStatus() const noexcept {return particles_?particles_->status:ParticleRenderStatus{};}
bool EntityScene::IsParticlePreview() const noexcept {return particles_ && particles_->preview;}
void EntityScene::UpdateParticles(float deltaSeconds,bool running,bool paused,EntityId previewEntity,unsigned previewBurst){
    auto bursts=std::exchange(animationParticleBursts_,{});
    if(!running){FinalizeParticles();return;}
    try {
        if(particles_ && particles_->preview!=previewEntity)FinalizeParticles();
        if(!particles_){
            particles_.reset(new ParticleSceneState);
            particles_->preview=previewEntity;
            particles_->eventPreview=previewEntity && previewBurst>0;
        }
        auto& state=*particles_;state.status={};state.drawFailure=false;
        if(!std::isfinite(deltaSeconds)||deltaSeconds<0){state.entries.clear();state.frame.items.clear();state.accumulator=0;state.status.result=RenderParticleResult::InvalidComponent;return;}
        const auto built=BuildRenderParticleFrame(world_,state.frame,previewEntity);state.status.result=built;
        if(built!=RenderParticleResult::Success){state.entries.clear();state.frame.items.clear();state.accumulator=0;return;}
        std::unordered_set<EntityId> live;for(const auto& item:state.frame.items)live.insert(item.id);
        std::erase_if(state.entries,[&](const auto& entry){return !live.contains(entry.first);});
        auto* manager=Particle::ParticleManager::GetInstance();manager->CollectRetiredGroups();
        for(const auto& item:state.frame.items){
            auto& entry=state.entries.try_emplace(item.id).first->second;std::string error;const auto resolved=ResolveParticle(item.settings,assets_,projectPaths_.Root(),error);
            const auto* asset=resolved.empty()?nullptr:assets_.FindByPath(resolved);const auto signature=EncodeParticle(item.settings).dump()+"\n"+Base::PathToUtf8(resolved)+"\n"+(asset?asset->importFingerprint:std::string{});
            if(signature==entry.signature){if(!entry.owner)state.status.result=RenderParticleResult::AssetFailure;continue;}
            entry.owner.Reset();entry.signature.clear();entry.name.clear();entry.remainder=0;entry.burst=0;
            try {
                if(resolved.empty())throw std::runtime_error(error);
                if(state.generation==(std::numeric_limits<uint64_t>::max)())throw std::overflow_error("Particle Scene owner names exhausted");
                ParticleSceneState::Entry prepared;prepared.signature=signature;prepared.name="__entity_particles/"+std::to_string(state.scope)+"/"+std::to_string(++state.generation)+"/"+std::to_string(item.id);prepared.burst=state.eventPreview?0:item.settings.burst;
                prepared.owner=manager->CreateOwnedParticleGroup(prepared.name,Base::PathToUtf8(resolved),Geometry(item.settings.geometry),std::make_unique<AuthoredParticleBehavior>(item.settings),item.settings.capacity);prepared.owner.SetLifetimeFade(false);
                static_assert(std::is_nothrow_move_assignable_v<ParticleSceneState::Entry>);entry=std::move(prepared);
            }catch(const std::exception& failure){entry.signature=signature;state.status.result=RenderParticleResult::AssetFailure;LogEntityDiagnostic(item.id,item.settings.assetGuid,"Particle Entity "+std::to_string(item.id)+": "+failure.what());}
        }
        if(previewEntity){
            if(auto it=state.entries.find(previewEntity);it!=state.entries.end() && it->second.owner)
                it->second.burst=(std::min)(4096u,it->second.burst+(std::min)(4096u,previewBurst));
        }else{
            for(const auto& [id,count]:bursts)
                if(auto it=state.entries.find(id);it!=state.entries.end() && it->second.owner)
                    it->second.burst=(std::min)(4096u,it->second.burst+count);
        }
        if(!paused){
            state.accumulator+=(std::min)(double(deltaSeconds),.25);size_t remaining=1024;
            while(state.accumulator+1e-12>=double(Step)&&state.status.steps<8){
                state.accumulator-=double(Step);++state.status.steps;
                for(const auto& item:state.frame.items){auto& entry=state.entries.at(item.id);if(!entry.owner)continue;
                    entry.owner.Update(Step);entry.remainder+=double(state.eventPreview?0:item.settings.rate)*double(Step);const auto periodic=static_cast<size_t>(std::floor(entry.remainder));entry.remainder-=double(periodic);const auto wanted=periodic+entry.burst;entry.burst=0;
                    const auto count=(std::min)({wanted,remaining,size_t(item.settings.capacity)});state.status.dropped+=wanted-count;remaining-=count;
                    const auto before=manager->GetActiveParticleCount(entry.name).value_or(0);if(count)entry.owner.Emit({0,0,0},static_cast<uint32_t>(count));const auto after=manager->GetActiveParticleCount(entry.name).value_or(0);state.status.dropped+=count-(after-before);entry.owner.Update(0);
                }
            }
            if(state.status.steps==8&&state.accumulator>=double(Step))state.accumulator=std::fmod(state.accumulator,double(Step));
        }
        for(const auto& [id,entry]:state.entries){(void)id;if(entry.owner){++state.status.emitters;state.status.particles+=manager->GetActiveParticleCount(entry.name).value_or(0);}}
    }catch(const std::bad_alloc&){if(particles_){particles_->entries.clear();particles_->frame.items.clear();particles_->status={};particles_->status.result=RenderParticleResult::AllocationFailure;}}
    catch(const std::exception& error){if(particles_){particles_->entries.clear();particles_->frame.items.clear();particles_->status={};particles_->status.result=RenderParticleResult::ResourceFailure;}Base::Logger::Log(std::string("Particle frame refused: ")+error.what(),Base::Logger::Level::Error);}
}
void EntityScene::DrawParticles(){
    if(!particles_)return;auto* camera=CameraSystem::CameraManager::GetInstance()->GetActiveCamera();if(!camera)return;auto& state=*particles_;
    for(const auto& item:state.frame.items){const auto found=state.entries.find(item.id);if(found==state.entries.end()||!found->second.owner)continue;
        try{found->second.owner.Draw(camera->GetViewMatrix(),camera->GetProjectionMatrix(),item.model);}
        catch(const std::exception& error){state.status.result=RenderParticleResult::ResourceFailure;if(!state.drawFailure)Base::Logger::Log(std::string("Particle draw refused: ")+error.what(),Base::Logger::Level::Error);state.drawFailure=true;}
    }
}
}
