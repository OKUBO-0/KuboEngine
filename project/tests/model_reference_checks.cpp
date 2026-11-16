#include "../engine/assets/ModelReference.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary); stream << bytes;
    Require(bool(stream), "Fixture write failed");
}
}
int main() {
    using namespace Engine;
    namespace fs = std::filesystem;
    const auto fixture = fs::temp_directory_path() / (L"kubo-model-reference-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto root = fixture / L"Assets_日本語";
        fs::create_directories(root);
        const auto source = root / L"モデル.gltf";
        Write(source, "model fixture, no GPU loader invoked");
        Assets::AssetRegistry registry;
        std::string error;
        Require(registry.Scan(root, error), error.c_str());
        const auto guid = registry.FindByPath(source)->guid;
        Scene::EntityWorld world;
        const auto id = world.CreateEntity("Model");
        const auto hint = fs::relative(source, fixture).generic_u8string();
        Require(world.SetModel(id, {hint.begin(), hint.end()}, guid), "Set GUID model");
        const auto saved = world.Capture();
        Scene::EntityWorld loaded;
        Require(loaded.Restore(saved, error), error.c_str());
        Require(loaded.Find(id)->model.assetGuid == guid, "GUID lost in roundtrip");
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, registry, fixture, error) == source, "Native Unicode resolve");
        const auto moved = root / L"移動.gltf";
        fs::rename(source, moved);
        fs::rename(Assets::AssetRegistry::MetadataPath(source), Assets::AssetRegistry::MetadataPath(moved));
        Write(source, "unrelated replacement");
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, registry, fixture, error).empty(), "Stale registry substituted old path");
        Require(registry.Scan(root, error), error.c_str());
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, registry, fixture, error) == moved, "GUID did not follow paired move");
        Assets::AssetRegistry player;
        Require(player.Scan(root, error, Assets::AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, player, fixture, error) == moved, "Player read-only GUID resolve");
        Write(Assets::AssetRegistry::MetadataPath(moved), "{\"version\":1,\"guid\":\"ffffffffffffffffffffffffffffffff\"}");
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, player, fixture, error).empty(), "Changed sidecar identity was accepted by stale registry");
        Write(Assets::AssetRegistry::MetadataPath(moved), std::string(17000, 'x'));
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, player, fixture, error).empty(), "Oversized sidecar was accepted");
        Write(Assets::AssetRegistry::MetadataPath(moved), "{\"version\":1,\"guid\":\"" + guid + "\"}");
        const auto relocated = fixture / "RelocatedProject";
        fs::create_directories(relocated);
        fs::copy(root, relocated / root.filename(), fs::copy_options::recursive);
        Assets::AssetRegistry packaged;
        Require(packaged.Scan(relocated / root.filename(), error, Assets::AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, packaged, relocated, error) == relocated / root.filename() / moved.filename(),
            "Relocated project resolved old project path");
        fs::remove(Assets::AssetRegistry::MetadataPath(moved));
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, player, fixture, error).empty(), "Missing sidecar was not rejected");
        Require(player.Scan(root, error, Assets::AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        Require(Assets::ResolveModelReference(loaded.Find(id)->model, player, fixture, error).empty(), "Missing GUID fell back to old-path replacement");
        Require(world.SetModel(id, {hint.begin(), hint.end()}), "Legacy setter");
        Require(world.Find(id)->model.assetGuid.empty(), "Path edit retained old GUID");
        Require(Assets::ResolveModelReference(world.Find(id)->model, registry, fixture, error) == source, "Legacy UTF-8 path resolve");
        auto invalid = saved;
        invalid["entities"][0]["modelGuid"] = "not-a-guid";
        const auto before = loaded.Capture().dump();
        Require(!loaded.Restore(invalid, error) && loaded.Capture().dump() == before, "Invalid GUID restore must be atomic");
        Require(!world.SetModel(id, "path", "BAD"), "Malformed GUID accepted");
        Require(world.SetModel(id, "", guid), "GUID-only model rejected");
        Require(loaded.Restore(world.Capture(), error) && loaded.Find(id)->hasModel, "GUID-only document restore");
        const auto script = root / "wrong.kubo";
        Write(script, "on_update translate 1 0 0");
        Require(registry.Scan(root, error), error.c_str());
        const auto scriptGuid = registry.FindByPath(script)->guid;
        Require(world.SetModel(id, "old.gltf", scriptGuid), "Valid format GUID rejected");
        Require(Assets::ResolveModelReference(world.Find(id)->model, registry, fixture, error).empty(), "Script GUID loaded as model");
        world.RemoveModel(id);
        Require(!world.Find(id)->hasModel && world.Find(id)->model.assetGuid.empty(), "Remove retained identity");
        fs::remove_all(fixture);
        std::cout << "PASS: model GUID persistence, native resolution, moved source, stale/missing identity rejection and legacy paths\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::error_code ec; fs::remove_all(fixture, ec); return 1;
    }
}
