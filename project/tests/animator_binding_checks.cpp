#include "engine/animation/AnimatorBinding.h"
#include <cstdlib>
#include <new>
#include <iostream>
namespace { bool failAllocation = false; void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); } }
void* operator new(size_t bytes) { if (failAllocation) { failAllocation = false; throw std::bad_alloc(); } if (auto* p = std::malloc(bytes ? bytes : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
int main() { try {
    using namespace Engine::AnimationSystem;
    AnimatorBinding live, prepared; AnimatorComponent config;
    config.clip = std::string(256, 'a'); config.speed = 2; config.playOnAwake = false;
    Require(live.Prepare(&config, true, true, prepared) && prepared.authored && !prepared.playing && prepared.NeedsNewOwner(nullptr), "Initial bind/playOnAwake failed");
    live.Swap(prepared); Require(live.Matches(&config) && !live.NeedsNewOwner(&config), "Binding mismatch");
    Require(live.Apply(AnimatorCommand::Play) && live.EffectiveSpeed() == 2, "Play failed");
    config.speed = 0.5f; config.loop = false;
    Require(live.Prepare(&config, true, false, prepared) && prepared.playing && prepared.EffectiveSpeed() == 0.5f, "Same clip settings reset playback");
    live.Swap(prepared); Require(live.Apply(AnimatorCommand::Pause), "Pause failed");
    config.playOnAwake = true;
    Require(live.Prepare(&config, true, false, prepared) && !prepared.playing, "Same clip playOnAwake restarted paused owner");
    live.Swap(prepared);
    Require(live.Prepare(&config, true, true, prepared) && prepared.playing, "Model replacement ignored playOnAwake");
    auto changed = config; changed.clip = "next"; changed.playOnAwake = false;
    Require(live.NeedsNewOwner(&changed) && live.Prepare(&changed, true, false, prepared) && !prepared.playing, "Clip replacement policy failed");
    const auto retained = EncodeAnimator(prepared.settings).dump(); const bool retainedPlaying = prepared.playing;
    Require(!live.Prepare(&config, false, false, prepared) && EncodeAnimator(prepared.settings).dump() == retained && prepared.playing == retainedPlaying, "Missing clip published intent");
    auto invalid = config; invalid.speed = -1;
    Require(!live.Prepare(&invalid, true, false, prepared) && EncodeAnimator(prepared.settings).dump() == retained, "Invalid config published intent");
    failAllocation = true; const auto result = live.Prepare(&config, true, false, prepared); failAllocation = false;
    Require(!result && EncodeAnimator(prepared.settings).dump() == retained && prepared.playing == retainedPlaying, "Allocation fault published intent");
    Require(live.Apply(AnimatorCommand::Stop) && !live.playing && live.Apply(AnimatorCommand::Restart) && live.playing, "Stop/Restart intent failed");
    Require(!live.Apply(static_cast<AnimatorCommand>(99)) && live.playing, "Invalid command changed intent");
    Require(live.Prepare(nullptr, false, false, prepared) && !prepared.authored && prepared.playing && prepared.EffectiveSpeed() == 1 && !prepared.Apply(AnimatorCommand::Play), "Legacy removal defaults failed");
    config.playOnAwake = false;live.ResetIntent(&config);
    Require(!live.playing, "Reset ignored pending same-clip playOnAwake");
    prepared.ResetIntent(&changed);Require(!prepared.playing && prepared.EffectiveSpeed()==0,"Reset thawed unbound Animator draft");
    prepared.ResetIntent(nullptr);Require(prepared.playing && prepared.EffectiveSpeed()==1,"Reset changed legacy autoplay");
    auto controller=config;controller.controllerGuid=std::string(32,'a');
    Require(!live.Matches(&controller) && live.NeedsNewOwner(&controller),"Controller GUID did not invalidate binding");
    Require(live.Prepare(&controller,true,false,prepared) && !prepared.playing,"New Controller ignored playOnAwake");live.Swap(prepared);
    Require(live.Matches(&controller) && !live.NeedsNewOwner(&controller),"Controller binding lost GUID");
    auto otherController=controller;otherController.controllerGuid=std::string(16,'b');
    Require(!live.Matches(&otherController) && live.NeedsNewOwner(&otherController),"Changed Controller reused owner");
    std::cout << "PASS: production Animator binding intent/new-owner policy, same-clip pause retention, model replacement, command admission and unchanged-output allocation/missing/invalid refusals (clock/pose remain Object3D; not GPU/Scene integration)\n";
    return 0;
} catch (const std::exception& error) { failAllocation = false; std::cerr << error.what() << '\n'; return 1; } }
