#pragma once
#include "AnimatorControllerRuntime.h"
#include "AnimationLayerPose.h"
#include "engine/3d/model/AnimationTime.h"

namespace Engine::AnimationSystem::Controller {
// Prepared copies hold all layer clocks/poses and the parameter/state Runtime.
// The render owner commits them only after validating hierarchy and palette.
class LayeredPlayer final {
public:
    struct Clock {
        float time=0,sourceTime=0,sourceSpeed=1,elapsed=0,duration=0;
        double normalized=0,observed=0;
        bool blending=false,sourceLoop=true;
        std::string sourceClip;
        std::vector<QuaternionTransform> snapshot,lastPose;
    };
    struct Layer {Clock clock;BoneMask mask;float weight=1;};
private:
    Skeleton bind_;
    std::vector<QuaternionTransform> reference_;
    std::vector<Layer> layers_;
public:
    const Skeleton& BindSkeleton() const noexcept {return bind_;}
    const Clock* GetClock(size_t layer) const noexcept {return layer<layers_.size()?&layers_[layer].clock:nullptr;}
    std::optional<float> Weight(size_t layer) const noexcept {return layer<layers_.size()?std::optional<float>(layers_[layer].weight):std::nullopt;}
    bool SetWeight(size_t layer,float weight) noexcept {
        if(layer>=layers_.size() || !std::isfinite(weight) || weight<0 || weight>1)return false;
        layers_[layer].weight=weight;return true;
    }
    void AdoptBaseClock(Clock clock) {layers_.at(0).clock=std::move(clock);}
    bool IsBlending() const noexcept {for(const auto& layer:layers_)if(layer.clock.blending)return true;return false;}
    void Reset() noexcept {for(auto& layer:layers_){auto& c=layer.clock;c.time=0;c.normalized=c.observed=0;c.blending=false;c.elapsed=0;c.snapshot.clear();c.lastPose.clear();}}
    template<class Lookup> static std::unique_ptr<LayeredPlayer> Prepare(const Runtime& runtime,const Skeleton& bind,Lookup&& lookup) {
        auto player=std::make_unique<LayeredPlayer>();player->bind_=bind;
        Animation identity{};if(!SampleLocalPose(bind,identity,0,player->reference_))return {};
        for(const auto& definition:runtime.Definition().layers) {
            Layer layer;layer.weight=definition.weight;
            if(!ResolveBoneMask(bind,definition.mask,layer.mask))return {};
            const auto mode=definition.mode=="override"?PoseLayerMode::Override:PoseLayerMode::Additive;
            for(const auto& state:definition.states) {
                const auto* clip=lookup(state.clip);std::vector<QuaternionTransform> sample,composed;
                if(!clip || clip->nodeAnimations.empty() || !SampleLocalPose(bind,*clip,0,sample) ||
                    !ComposeMaskedLayer(player->reference_,sample,player->reference_,layer.mask,mode,1,composed))return {};
            }
            player->layers_.push_back(std::move(layer));
        }
        return player;
    }
private:
    // Uses the caller-owned actual clip catalog; no predicted/parallel clock.
    // Authored layer order gives first admitted trigger use precedence.
    template<class Lookup> bool SampleFrame(Runtime& runtime,float delta,float rate,Lookup&& lookup,
        std::vector<QuaternionTransform>& output) {
        if(layers_.size()!=runtime.Definition().layers.size() || !std::isfinite(rate) || rate<0)return false;
        std::vector<QuaternionTransform> composed=reference_;
        for(size_t index=0;index<layers_.size();++index) {
            auto& layer=layers_[index];auto& clock=layer.clock;
            const auto& definition=runtime.Definition().layers[index];
            const auto* state=runtime.ActiveState(index);if(!state)return false;
            float speed=0;if(!LayerFloat(double(state->speed)*rate,speed))return false;
            const float step=std::isfinite(delta) && delta>0 && speed>0?delta:0;
            if(step>0) {
                bool rejected=false;
                const bool changed=runtime.Advance(index,clock.observed,clock.normalized,[&](const State& target,const Transition& transition){
                    const auto* clip=lookup(target.clip);std::vector<QuaternionTransform> admitted;
                    if(!clip || !SampleLocalPose(bind_,*clip,0,admitted)){rejected=true;return false;}
                    if(transition.blendSeconds>0) {
                        auto snapshot=clock.blending?clock.lastPose:std::vector<QuaternionTransform>{};
                        if(clock.blending && snapshot.size()!=reference_.size()){rejected=true;return false;}
                        clock.sourceClip=state->clip;clock.sourceTime=clock.time;clock.sourceSpeed=speed;clock.sourceLoop=state->loop;
                        clock.snapshot.swap(snapshot);clock.duration=transition.blendSeconds;clock.elapsed=0;clock.blending=true;
                    }else{clock.blending=false;clock.snapshot.clear();}
                    clock.time=0;clock.normalized=clock.observed=0;return true;
                });
                if(rejected)return false;
                clock.observed=changed?0:clock.normalized;
            }
            state=runtime.ActiveState(index);if(!LayerFloat(double(state->speed)*rate,speed))return false;
            const float targetStep=std::isfinite(delta) && delta>0 && speed>0?delta:0;
            const auto* target=lookup(state->clip);if(!target || target->nodeAnimations.empty() || !std::isfinite(target->duration) || target->duration<0)return false;
            const float nextTime=targetStep>0?Graphics3D::AdvanceAnimationSeconds(clock.time,target->duration,targetStep,speed,state->loop):clock.time;
            std::vector<QuaternionTransform> pose;
            if(targetStep==0 && !clock.lastPose.empty())pose=clock.lastPose;
            else if(clock.blending) {
                const float elapsed=(std::min)(clock.duration,clock.elapsed+targetStep);
                std::vector<QuaternionTransform> targetPose;
                if(!SampleLocalPose(bind_,*target,nextTime,targetPose))return false;
                if(!clock.snapshot.empty()) {
                    if(!BlendLocalPoses(clock.snapshot,targetPose,elapsed/clock.duration,pose))return false;
                }else{
                    const auto* source=lookup(clock.sourceClip);if(!source)return false;
                    const float nextSource=Graphics3D::AdvanceAnimationSeconds(clock.sourceTime,source->duration,targetStep,clock.sourceSpeed,clock.sourceLoop);
                    std::vector<QuaternionTransform> sourcePose;
                    if(!SampleLocalPose(bind_,*source,nextSource,sourcePose) || !BlendLocalPoses(sourcePose,targetPose,elapsed/clock.duration,pose))return false;
                    clock.sourceTime=nextSource;
                }
                clock.elapsed=elapsed;if(elapsed>=clock.duration)clock.blending=false;
            }else if(!SampleLocalPose(bind_,*target,clock.time,pose))return false;
            clock.time=nextTime;
            clock.normalized=Graphics3D::AdvanceControllerNormalizedTime(clock.normalized,nextTime,target->duration,targetStep,speed,state->loop);
            clock.lastPose=pose;
            const auto mode=definition.mode=="override"?PoseLayerMode::Override:PoseLayerMode::Additive;
            if(!ComposeMaskedLayer(composed,pose,reference_,layer.mask,mode,layer.weight,composed))return false;
        }
        output.swap(composed);return true;
    }
public:
    template<class Lookup> bool PrepareForcedFrame(const Runtime& runtime,size_t index,std::string_view name,
        float duration,float rate,Lookup&& lookup,std::unique_ptr<Runtime>& nextRuntime,
        std::unique_ptr<LayeredPlayer>& nextPlayer,std::vector<QuaternionTransform>& pose) const {
        if(index>=layers_.size() || !std::isfinite(rate) || rate<0)return false;
        auto candidateRuntime=runtime.Clone();if(!candidateRuntime)return false;
        auto candidatePlayer=std::make_unique<LayeredPlayer>(*this);
        const auto* outgoing=runtime.ActiveState(index);if(!outgoing)return false;
        auto& clock=candidatePlayer->layers_[index].clock;
        if(!candidateRuntime->ForceState(index,name,duration,[&](const State& target,const Transition&){
            const auto* clip=lookup(target.clip);std::vector<QuaternionTransform> admitted;
            if(!clip || clip->nodeAnimations.empty() || !SampleLocalPose(bind_,*clip,0,admitted))return false;
            float sourceSpeed=0;if(!LayerFloat(double(outgoing->speed)*rate,sourceSpeed))return false;
            if(duration>0) {
                std::vector<QuaternionTransform> raw;
                if(clock.blending){raw=clock.lastPose;if(raw.size()!=reference_.size())return false;}
                else {const auto* source=lookup(outgoing->clip);
                    if(!source || !SampleLocalPose(bind_,*source,clock.time,raw))return false;}
                auto snapshot=clock.blending?raw:std::vector<QuaternionTransform>{};
                clock.sourceClip=outgoing->clip;clock.sourceTime=clock.time;
                clock.sourceSpeed=sourceSpeed;clock.sourceLoop=outgoing->loop;
                clock.snapshot.swap(snapshot);clock.lastPose.swap(raw);clock.blending=true;
            }else{clock.blending=false;clock.snapshot.clear();clock.lastPose.clear();}
            clock.time=0;clock.normalized=clock.observed=0;clock.elapsed=0;clock.duration=duration;
            return true;
        }))return false;
        std::vector<QuaternionTransform> candidatePose;
        if(!candidatePlayer->SampleFrame(*candidateRuntime,0,rate,lookup,candidatePose))return false;
        nextRuntime.swap(candidateRuntime);nextPlayer.swap(candidatePlayer);pose.swap(candidatePose);return true;
    }
    template<class Lookup> bool PrepareFrame(const Runtime& runtime,float delta,float rate,Lookup&& lookup,
        std::unique_ptr<Runtime>& nextRuntime,std::unique_ptr<LayeredPlayer>& nextPlayer,
        std::vector<QuaternionTransform>& pose) const {
        auto candidateRuntime=runtime.Clone();if(!candidateRuntime)return false;
        auto candidatePlayer=std::make_unique<LayeredPlayer>(*this);std::vector<QuaternionTransform> candidatePose;
        if(!candidatePlayer->SampleFrame(*candidateRuntime,delta,rate,lookup,candidatePose))return false;
        nextRuntime.swap(candidateRuntime);nextPlayer.swap(candidatePlayer);pose.swap(candidatePose);return true;
    }
};
}
