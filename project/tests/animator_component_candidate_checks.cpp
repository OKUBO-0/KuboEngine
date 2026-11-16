#include "animator_component_candidate.h"
#include <iostream>
#include <vector>
static void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
int main() {
    using namespace AnimatorComponentCandidate;
    using Value = crude_json::value;
    try {
        Component initial, output;
        const auto defaults = Encode(initial);
        Require(Decode(Value::parse(defaults.dump()), output) && Encode(output).dump() == defaults.dump(), "Default authored roundtrip failed");
        Component authored;
        authored.clip = "\xE8\xB5\xB0\xE3\x82\x8B"; // UTF-8 Japanese clip name.
        authored.speed = 2.5f; authored.loop = false; authored.playOnAwake = false;
        const auto good = Encode(authored);
        Require(Decode(Value::parse(good.dump()), output) && Encode(output).dump() == good.dump(), "Unicode authored roundtrip failed");
        Component copied = output; copied.clip = "independent";
        Require(output.clip == authored.clip, "Copied owner shares mutable clip storage");
        std::vector<Value> invalid;
        invalid.push_back(Value(nullptr)); invalid.push_back(Value::parse("[]"));
        for (const auto speed : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), double((std::numeric_limits<float>::max)()) * 2}) {
            auto doc = good; doc["speed"] = speed; invalid.push_back(doc);
        }
        for (const auto* key : {"clip", "speed", "loop", "playOnAwake"}) {
            auto doc = good; doc[key] = nullptr; invalid.push_back(doc);
        }
        for (const auto& text : {std::string{}, std::string(257, 'a'), std::string("bad\0name", 8), std::string("\xED\xA0\x80", 3), std::string("\xC0\xAF", 2)}) {
            auto doc = good; doc["clip"] = text; invalid.push_back(doc);
        }
        auto runtime = good; runtime["time"] = 12.0; invalid.push_back(runtime);
        auto wrong = good; wrong["speed"] = true; invalid.push_back(wrong);
        const auto unchanged = Encode(output).dump();
        for (const auto& doc : invalid) Require(!Decode(doc, output) && Encode(output).dump() == unchanged, "Invalid schema mutated existing authored state");
        auto boundary = good; boundary["clip"] = std::string(256, 'a'); boundary["speed"] = 0.0;
        Require(Decode(boundary, output) && output.speed == 0 && output.clip.size() == 256, "Valid boundary was refused");
        boundary["speed"] = double((std::numeric_limits<float>::max)());
        Require(Decode(boundary, output) && output.speed == (std::numeric_limits<float>::max)(), "Finite speed boundary was refused");
        Component invalidOwner; invalidOwner.clip.clear(); bool refused = false;
        try { (void)Encode(invalidOwner); } catch (const std::invalid_argument&) { refused = true; }
        Require(refused, "Invalid authored write accepted");
        std::cout << "PASS: private Animator authored schema; Unicode/default roundtrip, independent clip ownership, " << invalid.size() << " refusal cases with unchanged output, speed/name bounds and no persisted runtime clock (not Scene/GPU/Inspector/Lua integration or allocation-fault sweep)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
