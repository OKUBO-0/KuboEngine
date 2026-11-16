#include "animator_playback_candidate.h"
#include <iostream>
static void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
int main() {
    using namespace AnimatorPlaybackCandidate;
    using AnimatorComponentCandidate::Component;
    try {
        State first, second;
        Require(!first.Apply(Command::Play) && !first.Tick(4, 1), "Unbound state accepted playback");
        Component cfg; cfg.clip = "walk"; cfg.speed = 2.5f;
        Require(first.Configure(cfg, true) && second.Configure(cfg, true), "Valid binding refused");
        Require(first.Tick(4, .5f) && first.time == 1.25f && second.time == 0, "Entity clocks are not independent");
        Require(first.Apply(Command::Pause) && first.Tick(4, 10) && first.time == 1.25f, "Pause advanced clock");
        Require(first.Apply(Command::Play) && first.Tick(4, .3f) && first.time == 2, "Play did not resume");
        cfg.speed = 0; cfg.playOnAwake = false;
        Require(first.Configure(cfg, true) && first.time == 2 && first.playing && first.Tick(4, 10) && first.time == 2,
            "Same-clip parameters reset time or reapplied awake state");
        Require(first.Apply(Command::Stop) && !first.playing && first.time == 0, "Stop did not reset and pause");
        cfg.speed = 1; cfg.loop = false;
        Require(first.Configure(cfg, true) && !first.playing, "Parameter edit unpaused stopped state");
        Require(first.Apply(Command::Restart) && first.Tick(4, 10) && first.time == 4 && first.Tick(4, 1) && first.time == 4,
            "Nonloop completion did not clamp");
        const auto oldCfg = AnimatorComponentCandidate::Encode(first.authored).dump();
        const auto oldTime = first.time; const auto oldPlaying = first.playing;
        auto missing = cfg; missing.clip = "missing";
        Require(!first.Configure(missing, false) && first.time == oldTime && first.playing == oldPlaying &&
            AnimatorComponentCandidate::Encode(first.authored).dump() == oldCfg, "Missing clip silently fell back or mutated state");
        auto invalid = cfg; invalid.speed = -1;
        Require(!first.Configure(invalid, true) && first.time == oldTime, "Invalid config mutated clock");
        Require(!first.Tick(0, 1) && !first.Tick(4, -1) && !first.Tick(4, std::numeric_limits<float>::infinity()) && first.time == oldTime,
            "Invalid tick mutated state");
        cfg.clip = "idle"; cfg.playOnAwake = false; cfg.loop = true;
        Require(first.Configure(cfg, true) && first.time == 0 && !first.playing, "New clip failed awake/reset policy");
        Require(first.Apply(Command::Play) && first.Tick(4, 9) && first.time == 1, "Loop did not wrap");
        Require(first.Configure(cfg, true) && first.time == 1 && first.playing, "Repeated binding reset clock");
        Require(first.Apply(Command::Restart) && first.time == 0 && second.time == 0, "Restart affected another Entity");
        const auto authored = AnimatorComponentCandidate::Encode(first.authored);
        Require(!authored.contains("time") && !authored.contains("playing"), "Runtime state leaked into authored JSON");
        std::cout << "PASS: private Animator policy with production time helper; independent clocks, resume/pause/stop/restart, same-clip edits, awake/new-clip policy, loop/clamp and missing/invalid no-mutation (not Object3D/Scene/Inspector/Lua/GPU integration)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
