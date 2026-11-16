#include "../engine/script/ScriptReference.h"
#include "../engine/editor/EntityHistory.h"
#include "../engine/scene/entity/WorldPlayback.h"
#include <Windows.h>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary); output << bytes;
    Require(bool(output), "Fixture write failed");
}
void Position(const Engine::Scene::EntityWorld& world, Engine::Scene::EntityId id, float x) {
    Require(std::abs(world.Find(id)->transform.position.x - x) < 0.001f, "Unexpected lifecycle translation");
}
}
int main() {
    using namespace Engine;
    namespace fs = std::filesystem;
    const auto fixture = fs::temp_directory_path() / (L"kubo-script-reference-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto root = fixture / L"Assets_日本語";
        fs::create_directories(root);
        const auto original = root / L"移動スクリプト.kubo";
        Write(original, "on_create translate 10 0 0\non_update translate 2 0 0\non_destroy translate 100 0 0\n");
        Assets::AssetRegistry registry;
        std::string error;
        Require(registry.Scan(root, error), error.c_str());
        const auto guid = registry.FindByPath(original)->guid;
        const auto relative = fs::relative(original, fixture).generic_u8string();
        Scene::EntityWorld world;
        const auto id = world.CreateEntity("Script entity");
        Require(world.SetScript(id, {relative.begin(), relative.end()}, true, guid), "Cannot assign GUID script");
        Scene::EntityWorld loaded;
        Require(loaded.Restore(world.Capture(), error) && loaded.Find(id)->script.assetGuid == guid, "GUID persistence failed");
        Editor::EntityHistory history;
        const auto authored = loaded.Capture();
        Require(loaded.SetScript(id, "legacy.kubo"), "History legacy assignment failed");
        Require(history.Record(authored, loaded, id, id, error), error.c_str());
        Scene::EntityId selection = id;
        Require(history.Undo(loaded, selection, error) && loaded.Find(id)->script.assetGuid == guid, "Undo lost script GUID");
        Require(history.Redo(loaded, selection, error) && loaded.Find(id)->script.assetGuid.empty(), "Redo kept script GUID");
        Require(history.Undo(loaded, selection, error), error.c_str());
        auto bad = world.Capture(); bad["entities"][0]["script"]["guid"] = 1.0;
        const auto before = loaded.Capture().dump();
        Require(!loaded.Restore(bad, error) && loaded.Capture().dump() == before, "Malformed GUID restore was not atomic");
        Require(!world.SetScript(id, "path", true, "BAD"), "Malformed GUID setter accepted");
        Scripting::ScriptRuntime runtime;
        Require(Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), error.c_str());
        runtime.Update(world, 0.5f); Position(world, id, 11);
        const auto moved = root / L"改名.kubo";
        fs::rename(original, moved);
        fs::rename(Assets::AssetRegistry::MetadataPath(original), Assets::AssetRegistry::MetadataPath(moved));
        Write(original, "on_update translate 1000 0 0\n");
        Require(!Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), "Stale registry ran replacement script");
        runtime.Update(world, 0.5f); Position(world, id, 111);
        Require(runtime.GetRunningCount() == 0 && !world.Find(id)->script.started, "Missing identity did not stop instance");
        runtime.Update(world, 0.5f); Position(world, id, 111);
        Require(registry.Scan(root, error), error.c_str());
        Require(Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), error.c_str());
        runtime.Update(world, 0.5f); Position(world, id, 122);
        // A refreshed paired rename keeps the running GUID instance, not a path-keyed restart.
        const auto renamedAgain = root / "again.kubo";
        fs::rename(moved, renamedAgain);
        fs::rename(Assets::AssetRegistry::MetadataPath(moved), Assets::AssetRegistry::MetadataPath(renamedAgain));
        Require(registry.Scan(root, error), error.c_str());
        Require(Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), error.c_str());
        runtime.Update(world, 0.5f); Position(world, id, 123);
        fs::remove(Assets::AssetRegistry::MetadataPath(renamedAgain));
        Require(!Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), "Missing sidecar ran cached code");
        runtime.Update(world, 0.5f); Position(world, id, 223);
        runtime.Shutdown(world); Position(world, id, 223);
        // Recover without changing the authored GUID; Player resolves native Unicode paths.
        Write(Assets::AssetRegistry::MetadataPath(renamedAgain), "{\"version\":1,\"guid\":\"" + guid + "\"}");
        Assets::AssetRegistry player;
        Require(player.Scan(root, error, Assets::AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        const auto relocated = fixture / "Relocated";
        fs::create_directories(relocated);
        fs::copy(root, relocated / root.filename(), fs::copy_options::recursive);
        Assets::AssetRegistry relocatedRegistry;
        Require(relocatedRegistry.Scan(relocated / root.filename(), error, Assets::AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        Scene::EntityWorld relocatedWorld;
        Require(relocatedWorld.Restore(loaded.Capture(), error), error.c_str());
        Scripting::ScriptRuntime relocatedRuntime;
        Require(Scripting::LoadScriptWorldAssets(relocatedRuntime, relocatedWorld, relocated, relocatedRegistry, error), error.c_str());
        relocatedRuntime.Update(relocatedWorld, 0.5f); Position(relocatedWorld, id, 11);
        relocatedRuntime.Shutdown(relocatedWorld); Position(relocatedWorld, id, 111);
        Scripting::ScriptRuntime playerRuntime;
        Require(Scripting::LoadScriptWorldAssets(playerRuntime, loaded, fixture, player, error), error.c_str());
        playerRuntime.Update(loaded, 0.5f); Position(loaded, id, 11);
        loaded.Find(id)->script.enabled = false;
        playerRuntime.Update(loaded, 0.5f); Position(loaded, id, 111);
        playerRuntime.Shutdown(loaded); Position(loaded, id, 111);
        loaded.Find(id)->script.enabled = true;
        Require(loaded.SetScript(id, "", true, guid), "GUID-only script setter rejected");
        Scene::EntityWorld guidOnly;
        Require(guidOnly.Restore(loaded.Capture(), error) && guidOnly.Find(id)->hasScript, "GUID-only script restore failed");
        Require(Scripting::LoadScriptWorldAssets(playerRuntime, guidOnly, fixture, player, error), error.c_str());
        playerRuntime.Update(guidOnly, 0.5f); Position(guidOnly, id, 122);
        // Explicit reload swaps callbacks on next Update, retaining old onDestroy exactly once.
        Write(renamedAgain, "on_create translate 20 0 0\non_update translate 4 0 0\non_destroy translate 200 0 0\n");
        Require(Scripting::LoadScriptReference(playerRuntime, guidOnly.Find(id)->script, fixture, player, error, true), error.c_str());
        playerRuntime.Update(guidOnly, 0.5f); Position(guidOnly, id, 244);
        Write(renamedAgain, "on_update translate invalid 0 0\n");
        Require(!Scripting::LoadScriptReference(playerRuntime, guidOnly.Find(id)->script, fixture, player, error, true), "Invalid script accepted");
        for (const auto& invalid : {std::string("on_update"), std::string("on_update translate 1e99 0 0"), std::string(1024 * 1024 + 1, 'x')}) {
            Write(renamedAgain, invalid);
            Require(!Scripting::LoadScriptReference(playerRuntime, guidOnly.Find(id)->script, fixture, player, error, true), "Malformed/oversized script accepted");
        }
        playerRuntime.Update(guidOnly, 0.5f); Position(guidOnly, id, 246);
        playerRuntime.Shutdown(guidOnly); Position(guidOnly, id, 446);
        // Legacy path assignment clears identity, and remains executable.
        Require(world.SetScript(id, "Assets_日本語/移動スクリプト.kubo"), "Legacy setter failed");
        Require(world.Find(id)->script.assetGuid.empty(), "Legacy edit retained GUID");
        Require(Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), error.c_str());
        runtime.Update(world, 0.5f); Position(world, id, 723);
        runtime.Shutdown(world);
        Write(original, "\xEF\xBB\xBF" "on_update translate 3 0 0\n");
        Require(Scripting::LoadScriptReference(runtime, world.Find(id)->script, fixture, registry, error, true), "UTF-8 BOM script rejected");
        runtime.Update(world, 1); Position(world, id, 726);
        runtime.Shutdown(world);
        const auto nonScript = root / "wrong.obj";
        Write(nonScript, "model");
        Require(registry.Scan(root, error), error.c_str());
        Require(world.SetScript(id, "old.kubo", true, registry.FindByPath(nonScript)->guid), "Valid GUID format rejected");
        Require(!Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), "Model GUID loaded as script");
        const auto healthyId = world.CreateEntity("Healthy script beside broken GUID");
        Require(world.SetScript(healthyId, "Assets_日本語/移動スクリプト.kubo"), "Healthy script assignment failed");
        Require(!Scripting::LoadScriptWorldAssets(runtime, world, fixture, registry, error), "Broken GUID unexpectedly resolved");
        runtime.Update(world, 0.5f); Position(world, healthyId, 1.5f); Position(world, id, 726);
        runtime.Shutdown(world);
        Write(renamedAgain, "on_create translate 10 0 0\non_update translate 2 0 0\non_destroy translate 100 0 0\n");
        Scene::EntityWorld playWorld;
        Require(playWorld.Restore(authored, error), error.c_str());
        Scripting::ScriptRuntime playRuntime;
        Scene::WorldPlayback playback;
        Physics::PhysicsWorld physics;
        Require(playback.Play(playWorld), "GUID Play failed");
        Require(Scripting::LoadScriptWorldAssets(playRuntime, playWorld, fixture, registry, error), error.c_str());
        playRuntime.Update(playWorld, playback.SimulationDelta(0.5f)); Position(playWorld, id, 11);
        physics.Advance(playWorld,1.0f/240);const auto pausedFraction=physics.GetAdvanceUsage().remainingSeconds;
        playback.Pause();
        physics.Advance(playWorld,playback.SimulationDelta(0.5f));Require(pausedFraction>0 && physics.GetAdvanceUsage().steps==0 && physics.GetAdvanceUsage().remainingSeconds==pausedFraction,"Paused playback consumed fixed physics time");
        Require(playback.SimulationDelta(0.5f) == 0, "Paused GUID script simulation advanced");
        Require(playback.Stop(playWorld, playRuntime, physics, error), error.c_str());
        Require(physics.GetAdvanceUsage().remainingSeconds==0 && physics.GetAdvanceUsage().steps==0,"Playback Stop retained physics fractional time");
        Require(playWorld.Capture().dump() == authored.dump() && playRuntime.GetRunningCount() == 0 && !playWorld.Find(id)->script.started,
            "Stop failed to restore authored GUID/transform and retire script");
        world.RemoveScript(id);
        Require(world.Find(id)->script.assetGuid.empty() && !world.Find(id)->hasScript, "Remove retained script reference");
        Scene::EntityWorld diagnosticWorld;Scripting::ScriptRuntime diagnostics;
        const auto missing=diagnosticWorld.CreateEntity("Missing"),badSource=diagnosticWorld.CreateEntity("Invalid source"),healthy=diagnosticWorld.CreateEntity("Healthy later peer"),disabled=diagnosticWorld.CreateEntity("Disabled");
        Write(fixture/"invalid.kubo","unknown operation\n");Write(fixture/"healthy.kubo","on_update translate 2 0 0\n");
        diagnosticWorld.SetScript(missing,"missing.kubo");diagnosticWorld.SetScript(badSource,"invalid.kubo");diagnosticWorld.SetScript(healthy,"healthy.kubo");diagnosticWorld.SetScript(disabled,"also-missing.kubo",false);
        const auto diagnosticBefore=diagnosticWorld.Capture().dump();const auto diagnosticHigh=diagnosticWorld.AllocationHighWatermark();
        Require(!Scripting::LoadScriptWorldAssets(diagnostics,diagnosticWorld,fixture,registry,error) && error.starts_with("Entity "+std::to_string(missing)+" [missing.kubo]:") && error.find("2 script load failures")!=std::string::npos,"World diagnostic lacks failed Entity/reference/count");
        Require(!diagnostics.GetAssetLoadError("missing.kubo").empty() && !diagnostics.GetAssetLoadError("invalid.kubo").empty() && diagnostics.GetAssetLoadError("healthy.kubo").empty() && diagnostics.GetAssetLoadError("also-missing.kubo").empty(),"Source diagnostic cache omitted missing/syntax errors or loaded disabled peer");
        Require(diagnostics.Contains("healthy.kubo") && diagnosticWorld.Capture().dump()==diagnosticBefore && diagnosticWorld.AllocationHighWatermark()==diagnosticHigh,"Diagnostic loading stopped healthy peer or modified authored World");
        diagnostics.Update(diagnosticWorld,0.5f);Position(diagnosticWorld,healthy,1);
        Write(fixture/"missing.kubo","on_update translate 1 0 0\n");Write(fixture/"invalid.kubo","on_update translate 3 0 0\n");
        Require(Scripting::LoadScriptWorldAssets(diagnostics,diagnosticWorld,fixture,registry,error) && error.empty() && diagnostics.GetAssetLoadError("missing.kubo").empty() && diagnostics.GetAssetLoadError("invalid.kubo").empty(),"Recovered source load left stale diagnostics");
        Write(fixture/"healthy.kubo","invalid syntax\n");Require(!diagnostics.LoadResolvedAsset("healthy.kubo",fixture/"healthy.kubo",error) && diagnostics.Contains("healthy.kubo") && !diagnostics.GetAssetLoadError("healthy.kubo").empty(),"Rejected reload lost old registration/last-load diagnostic");
        diagnostics.Update(diagnosticWorld,0.5f);Position(diagnosticWorld,healthy,2);diagnostics.Shutdown(diagnosticWorld);diagnostics.Clear();Require(diagnostics.GetAssetLoadError("healthy.kubo").empty(),"Clear retained source diagnostic");
        Scripting::ScriptRuntime legacyDiagnostics;Require(!legacyDiagnostics.LoadWorldAssets(diagnosticWorld,fixture,error) && error.starts_with("Entity "+std::to_string(healthy)) && legacyDiagnostics.Contains("missing.kubo"),"Legacy diagnostic missed failing Entity/healthy earlier peer");
        // Move a missing source ahead of a valid source: legacy loader must not
        // return early before registering the later healthy Entity.
        diagnosticWorld.SetScript(missing,"gone.kubo");Write(fixture/"healthy.kubo","on_update translate 2 0 0\n");legacyDiagnostics.Clear();
        Require(!legacyDiagnostics.LoadWorldAssets(diagnosticWorld,fixture,error) && error.starts_with("Entity "+std::to_string(missing)) && legacyDiagnostics.Contains("healthy.kubo"),"Legacy source failure stopped later healthy registration");
        Scripting::ScriptRuntime boundedDiagnostics;for(int n=0;n<270;++n)boundedDiagnostics.LoadResolvedAsset("key"+std::to_string(n),fixture/"gone.kubo",error);
        Require(!boundedDiagnostics.GetAssetLoadError("key0").empty() && !boundedDiagnostics.GetAssetLoadError("key255").empty() && boundedDiagnostics.GetAssetLoadError("key256").empty(),"Source diagnostic count is not bounded to 256");
        boundedDiagnostics.Unregister("key0");Require(boundedDiagnostics.GetAssetLoadError("key0").empty(),"Unregister retained diagnostic");
        const std::string oversizedKey(4097,'x');boundedDiagnostics.LoadResolvedAsset(oversizedKey,fixture/"gone.kubo",error);Require(boundedDiagnostics.GetAssetLoadError(oversizedKey).empty(),"Diagnostic cache retained oversized registration key");
        std::string unicode;for(int n=0;n<1000;++n)unicode+="日本語🚀";const auto description=Scripting::ScriptRuntime::DescribeLoadFailure(missing,unicode,unicode);
        Require(description.size()<2400 && description.find("...")!=std::string::npos && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,description.data(),static_cast<int>(description.size()),nullptr,0)>0,"Diagnostic clipping broke UTF-8 or byte bound");
        fs::remove_all(fixture);
        std::cout << "PASS: script GUID persistence, Unicode load, rename, fail-closed cached lifecycle, reload and legacy paths\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::error_code ec; fs::remove_all(fixture, ec); return 1;
    }
}
