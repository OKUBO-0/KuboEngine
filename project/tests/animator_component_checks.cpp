#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/scene/entity/EntityWorld.h"
#include "engine/editor/EntityHistory.h"
#include "engine/editor/PrefabDocument.h"
#include "engine/editor/PrefabMerge.h"
#include "engine/animation/AnimationClipCatalog.h"
#include <iostream>
#include <cstdlib>
#include <new>
namespace { long failAfter = -1; bool countAllocations = false; size_t allocationCount = 0;
void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
}
void* operator new(size_t size) {
    if (countAllocations) ++allocationCount;
    if (failAfter == 0) { failAfter = -1; throw std::bad_alloc(); }
    if (failAfter > 0) --failAfter;
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
int main() {
    using namespace Engine;
    using Value = Scene::SceneValue;
    try {
        struct Clip {float duration=1;std::vector<int> nodeAnimations{1};};
        std::map<std::string,Clip> clips{{"walk",{}},{"Idle",{}},{"default",{}},{"走る",{}},{"hash##clip",{}}};
        Clip fallback;
        Require(AnimationSystem::CollectAnimationClipNames(clips,fallback)==std::vector<std::string>{"default","Idle","hash##clip","walk","走る"},"Loaded clip sorting/default/Unicode names changed");
        clips[""]=Clip{};clips[std::string("bad\0name",8)]=Clip{};clips[std::string(257,'a')]=Clip{};
        clips[std::string(1,static_cast<char>(0xff))]=Clip{};
        clips["empty"].nodeAnimations.clear();clips["invalid"].duration=std::numeric_limits<float>::quiet_NaN();
        Require(AnimationSystem::CollectAnimationClipNames(clips,fallback).size()==5,"Invalid loaded clip advertised");
        clips.clear();Require(AnimationSystem::CollectAnimationClipNames(clips,fallback)==std::vector<std::string>{"default"},"Legacy default animation not discoverable");
        clips["default"].nodeAnimations.clear();Require(AnimationSystem::CollectAnimationClipNames(clips,fallback).empty(),"Masked default alias advertised fallback");
        clips.clear();fallback.nodeAnimations.clear();Require(AnimationSystem::CollectAnimationClipNames(clips,fallback).empty(),"Static model advertised an animation");
        Scene::EntityWorld world; std::string error;
        const auto root = world.CreateEntity("Animator owner");
        const auto child = world.CreateEntity("Child", root);
        const auto legacy = world.Capture();
        Require(!legacy["entities"][0].contains("animator"), "Absent Animator changed legacy serialization");
        Scene::EntityWorld old; Require(old.Restore(legacy, error) && !old.Find(root)->hasAnimator, "Legacy Scene migration failed");
        AnimationSystem::AnimatorComponent cfg; cfg.clip = "\xE8\xB5\xB0\xE3\x82\x8B"; cfg.speed = 2.5f; cfg.loop = false; cfg.playOnAwake = false;
        auto eventSettings=cfg;
        eventSettings.particleEvents={{cfg.clip,.25,4},{cfg.clip,1,2},{"other",.5,9}};
        AnimationSystem::AnimatorComponent eventReload;
        Require(AnimationSystem::DecodeAnimator(AnimationSystem::EncodeAnimator(eventSettings),eventReload) &&
            eventReload.particleEvents.size()==3 && eventReload.particleEvents[0].clip==cfg.clip,"Particle events failed UTF-8 roundtrip");
        using AnimationSystem::CountAnimationParticles;
        Require(CountAnimationParticles(eventReload,cfg.clip,0,.25,false,1024)==4 &&
            CountAnimationParticles(eventReload,cfg.clip,.25,1,false,1024)==2 &&
            CountAnimationParticles(eventReload,cfg.clip,1,1,false,1024)==0 &&
            CountAnimationParticles(eventReload,cfg.clip,.9,2.3,true,1024)==12 &&
            CountAnimationParticles(eventReload,cfg.clip,0,10000,true,1024)==1024,
            "Animation event boundary, loop or budget accounting");
        auto badEvent=AnimationSystem::EncodeAnimator(eventSettings);
        badEvent["particleEvents"][0][1]=0.0;
        Require(!AnimationSystem::DecodeAnimator(badEvent,eventReload) && eventReload.particleEvents[0].position==.25,
            "Malformed Particle event changed live settings");
        Scene::EntityWorld markedPrefabWorld;
        const auto markedEntity=markedPrefabWorld.CreateEntity("Marked Animator");
        Value markedPrefab;
        Require(markedPrefabWorld.SetAnimator(markedEntity,eventReload) &&
            Editor::CapturePrefabSubtree(markedPrefabWorld,markedEntity,markedPrefab,error),"Particle event Prefab capture failed");
        Scene::EntityWorld markedReload;
        Require(markedReload.Restore(markedPrefab["world"],error) &&
            markedReload.GetEntities().front().animator.particleEvents.size()==3,"Prefab roundtrip lost animation events");
        Require(world.SetAnimator(root, cfg), "Draft Animator without Model refused");
        const auto authored = world.Capture();
        Require(authored["entities"][0]["animator"].get<crude_json::object>().size() == 4, "Runtime fields persisted");
        Require(old.Restore(authored, error) && old.Find(root)->hasAnimator && old.Capture().dump() == authored.dump(), "Animator Scene roundtrip failed");
        auto independent = *world.Find(root); independent.animator.clip = "independent";
        Require(world.Find(root)->animator.clip == cfg.clip, "Record copy changed original clip owner");
        AnimationSystem::AnimatorComponent invalid = cfg; invalid.speed = -1;
        const auto snapshot = world.Capture().dump(); const auto high = world.AllocationHighWatermark(); const auto* pointer = world.Find(root);
        Require(!world.SetAnimator(root, invalid) && !world.SetAnimator(777, cfg) && world.Capture().dump() == snapshot, "Invalid setter mutated World");
        AnimationSystem::AnimatorComponent longCfg = cfg; longCfg.clip = std::string(256, 'a');
        failAfter = 0; const auto set = world.SetAnimator(root, longCfg); failAfter = -1;
        Require(!set && world.Capture().dump() == snapshot && world.Find(root) == pointer, "Setter allocation failure partially published");
        std::vector<Value> malformed;
        malformed.push_back(Value(nullptr)); malformed.push_back(Value::parse("[]"));
        for (const auto speed : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), double((std::numeric_limits<float>::max)()) * 2}) {
            auto value = AnimationSystem::EncodeAnimator(cfg); value["speed"] = speed; malformed.push_back(value);
        }
        for (const auto* key : {"clip", "speed", "loop", "playOnAwake"}) {
            auto value = AnimationSystem::EncodeAnimator(cfg); value[key] = nullptr; malformed.push_back(value);
        }
        for (const auto& text : {std::string{}, std::string(257, 'a'), std::string("a\0b", 3), std::string("\xED\xA0\x80", 3)}) {
            auto value = AnimationSystem::EncodeAnimator(cfg); value["clip"] = text; malformed.push_back(value);
        }
        auto clock = AnimationSystem::EncodeAnimator(cfg); clock["time"] = 3.0; malformed.push_back(clock);
        for (const auto& value : malformed) {
            auto scene = authored; scene["entities"][0]["animator"] = value;
            Require(!world.Restore(scene, error) && world.Capture().dump() == snapshot && world.Find(root) == pointer && world.AllocationHighWatermark() == high,
                "Invalid Animator restore changed live storage/allocator");
        }
        Editor::EntityHistory history; Scene::EntityId selection = root;
        world.RemoveAnimator(root);
        Require(history.Record(authored, world, root, root, error), error.c_str()); history.Commit();
        Require(history.Undo(world, selection, error) && world.Find(root)->hasAnimator && world.Capture().dump() == snapshot, "Animator removal Undo failed");
        Require(history.Redo(world, selection, error) && !world.Find(root)->hasAnimator, "Animator removal Redo failed");
        Require(world.SetAnimator(root, longCfg), "Long clip setter failed");
        Scene::EntityId clone = 0;
        Require(world.DuplicateSubtree(root, 0, clone, error) && world.Find(clone)->hasAnimator && world.Find(clone)->animator.clip == longCfg.clip,
            "Subtree clone lost Animator");
        Value prefab; Require(Editor::CapturePrefabSubtree(world, root, prefab, error), error.c_str());
        Value normalized; Require(Editor::ValidatePrefabDocument(prefab, normalized, error) && normalized.dump() == prefab.dump(), "Prefab normalization lost Animator");
        auto incoming = prefab, mine = prefab;
        incoming["world"]["entities"][0]["animator"]["loop"] = true;
        mine["world"]["entities"][0]["animator"]["speed"] = 0.75;
        Value merged; Editor::PrefabMergeReport report;
        Require(Editor::MergePrefabDocuments(prefab, incoming, mine, merged, report, error), error.c_str());
        const auto& mergedAnimator = merged["world"]["entities"][0]["animator"];
        Require(mergedAnimator["loop"].get<crude_json::boolean>() && mergedAnimator["speed"].get<crude_json::number>() == 0.75 &&
            mergedAnimator["clip"].get<crude_json::string>() == longCfg.clip, "Prefab merge lost inherited field or Animator override");
        const auto directory = std::filesystem::temp_directory_path() / (L"kubo-animator-component-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(!std::filesystem::exists(directory), "Fresh file fixture required"); std::filesystem::create_directories(directory);
        Require(world.Save(directory / "saved.scene", error), error.c_str()); Scene::EntityWorld loaded;
        Require(loaded.Load(directory / "saved.scene", error) && loaded.Capture().dump() == world.Capture().dump(), "Actual Scene file lost Animator");
        const auto valid = world.Capture(); const auto beforeFault = valid.dump(); const auto faultHigh = world.AllocationHighWatermark(); const auto* faultPointer = world.Find(root);
        Scene::EntityWorld counted; allocationCount = 0; countAllocations = true;
        const auto baseline = counted.Restore(valid, error); countAllocations = false; const auto positions = allocationCount;
        Require(baseline && positions > 0, "Restore allocation baseline failed");
        for (size_t position = 0; position < positions; ++position) {
            failAfter = static_cast<long>(position); const auto restored = world.Restore(valid, error); failAfter = -1;
            Require(!restored && world.Capture().dump() == beforeFault && world.Find(root) == faultPointer && world.AllocationHighWatermark() == faultHigh,
                "Restore allocation failure partially published Animator World");
        }
        (void)child;
        // The Inspector uses this existing frame-grouped history path; exercise
        // all authored fields without claiming mouse/widget automation coverage.
        Scene::EntityWorld edited; const auto selected = edited.CreateEntity("Inspector primary");
        const auto untouched = edited.CreateEntity("Other selection");
        Editor::EntityHistory edits; Scene::EntityId primary = selected;
        auto beforeEdit = edited.Capture();
        Require(edited.SetAnimator(selected, {}), "Inspector-style add failed");
        Require(edits.Record(beforeEdit, edited, primary, primary, error), error.c_str()); edits.Commit();
        Require(edits.Undo(edited, primary, error) && !edited.Find(selected)->hasAnimator && primary == selected, "Animator add Undo/selection failed");
        Require(edits.Redo(edited, primary, error) && edited.Find(selected)->hasAnimator, "Animator add Redo failed");
        const auto editBaseline = edited.Capture(); beforeEdit = editBaseline;
        auto settings = edited.Find(selected)->animator; settings.speed = 0.5f;
        Require(edited.SetAnimator(selected, settings) && edits.Record(beforeEdit, edited, primary, primary, error), "Speed edit history failed");
        beforeEdit = edited.Capture(); settings.clip = "edited clip"; settings.speed = 3.0f; settings.loop = false; settings.playOnAwake = false;
        Require(edited.SetAnimator(selected, settings) && edits.Record(beforeEdit, edited, primary, primary, error), "Grouped Animator field edit failed"); edits.Commit();
        const auto editResult = edited.Capture().dump();
        Require(edits.UndoCount() == 2 && edits.Undo(edited, primary, error) && edited.Capture().dump() == editBaseline.dump(), "Grouped Animator settings Undo failed");
        Require(edits.Redo(edited, primary, error) && edited.Capture().dump() == editResult && !edited.Find(untouched)->hasAnimator, "Grouped Animator Redo/primary-only edit failed");
        auto rejected = settings; rejected.clip.clear();
        const auto historyCount = edits.UndoCount();
        Require(!edited.SetAnimator(selected, rejected) && edits.Record(edited.Capture(), edited, primary, primary, error), "Rejected Inspector-style edit failed"); edits.Commit();
        Require(edits.UndoCount() == historyCount && edited.Capture().dump() == editResult, "Rejected Animator settings created history or changed World");
        auto controllerSettings=settings;controllerSettings.controllerGuid=std::string(32,'a');
        const auto beforeGuid=edited.Capture();
        Require(edited.SetAnimator(selected,controllerSettings) && edits.Record(beforeGuid,edited,primary,primary,error),"Controller assignment history failed");edits.Commit();
        const auto controllerScene=edited.Capture();
        Require(controllerScene["entities"][0]["animator"]["controllerGuid"].get<crude_json::string>()==controllerSettings.controllerGuid,"Controller GUID not serialized");
        Require(edits.Undo(edited,primary,error) && edited.Find(selected)->animator.controllerGuid.empty() && edits.Redo(edited,primary,error) && edited.Find(selected)->animator.controllerGuid==controllerSettings.controllerGuid,"Controller GUID Undo/Redo failed");
        Require(edited.Save(directory/"controller.scene",error) && loaded.Load(directory/"controller.scene",error) && loaded.Capture().dump()==controllerScene.dump(),"Controller Scene file roundtrip failed");
        Value controllerPrefab,normalizedController;Require(Editor::CapturePrefabSubtree(edited,selected,controllerPrefab,error) && Editor::ValidatePrefabDocument(controllerPrefab,normalizedController,error) && normalizedController.dump()==controllerPrefab.dump(),"Controller Prefab normalization failed");
        Scene::EntityId controllerClone=0;Require(edited.DuplicateSubtree(selected,0,controllerClone,error) && edited.Find(controllerClone)->animator.controllerGuid==controllerSettings.controllerGuid,"Controller clone lost GUID");
        auto controllerIncoming=controllerPrefab;controllerIncoming["world"]["entities"][0]["animator"]["controllerGuid"]=std::string(16,'b');
        Require(Editor::MergePrefabDocuments(controllerPrefab,controllerIncoming,controllerPrefab,merged,report,error) && merged["world"]["entities"][0]["animator"]["controllerGuid"].get<crude_json::string>()==std::string(16,'b'),"Controller inherited GUID merge failed");
        for(const auto& badGuid:{std::string("../bad"),std::string(32,'A'),std::string(33,'a'),std::string("a\0b",3)}) {
            auto bad=controllerScene;bad["entities"][0]["animator"]["controllerGuid"]=badGuid;
            const auto retained=loaded.Capture().dump();Require(!loaded.Restore(bad,error) && loaded.Capture().dump()==retained,"Invalid Controller GUID changed World");
        }
        std::cout << "PASS: production Animator authored World/schema/legacy/file/clone/history/Prefab normalization and inherited-field/override merge; " << malformed.size() << " malformed refusals and " << positions << " ordinary restore allocation faults, setter allocation refusal (not runtime/Inspector/Lua/full native Prefab Apply/GPU integration) retained=" << directory << '\n';
    } catch (const std::exception& failure) { failAfter = -1; countAllocations = false; std::cerr << failure.what() << '\n'; return 1; }
}
