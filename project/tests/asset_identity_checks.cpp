#include "../engine/assets/AssetRegistry.h"
#include "../externals/imgui_node_editor/crude_json.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    Require(bool(output), "Fixture write failed");
}
}
int main()
{
    using namespace Engine::Assets;
    using J = crude_json::value;
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-asset-identity-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto root = fixture / L"Assets_日本語";
        std::filesystem::create_directories(root / "Models");
        const auto original = root / L"Models/モデル.gltf";
        const auto script = root / "mover.kubo";
        Write(original, "model");
        Write(script, "on_update translate 1 0 0");
        std::string error;
        AssetRegistry registry;
        Require(registry.Scan(root, error), error.c_str());
        Require(registry.GetAssets().size() == 2, "Sidecars were exposed as assets");
        const auto* record = registry.FindByPath(original);
        Require(record && record->guid.size() == 32 && record->relativePath.find("モデル") != std::string::npos,
            "Random identity or UTF-8 path was not recorded");
        const auto guid = record->guid;
        const auto fingerprint = record->fingerprint;
        Require(registry.ResolveGuid(guid) == original, "GUID resolution did not produce a native Unicode path");
        Require(registry.Search("", AssetType::Script).size() == 1, "Text operation .kubo script was classified as a scene");
        const auto metadata = AssetRegistry::MetadataPath(original);
        const auto metadataText = Read(metadata);
        const auto metadataTime = std::filesystem::last_write_time(metadata);
        AssetRegistry fresh;
        Require(fresh.Scan(root, error, AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        Require(fresh.FindByGuid(guid) && Read(metadata) == metadataText && std::filesystem::last_write_time(metadata) == metadataTime,
            "Read-only restart changed asset identity metadata");
        AssetRecord imported;
        Require(registry.ImportAsset(original, root / "Library/Imported", imported, error), error.c_str());
        const auto cache = imported.cachePath;

        std::filesystem::create_directories(root / "Moved");
        const auto moved = root / L"Moved/改名.gltf";
        std::filesystem::rename(original, moved);
        std::filesystem::rename(metadata, AssetRegistry::MetadataPath(moved));
        // Replacement occurs before refreshing the stale path-based cache.
        Write(original, "replacement");
        Require(registry.Scan(root, error), error.c_str());
        Require(registry.GetAssets().size() == 3 && registry.ResolveGuid(guid) == moved, "Move/rename lost persistent identity");
        Require(registry.FindByGuid(guid)->cachePath == cache && registry.FindByGuid(guid)->fingerprint != fingerprint,
            "Move did not retain GUID cache or refresh path fingerprint");
        Require(registry.Scan(root, error), error.c_str());
        Require(registry.FindByPath(original)->guid != guid && registry.ResolveGuid(guid) == moved,
            "Replacement at old path hijacked the moved asset identity");
        AssetRegistry restarted;
        Require(restarted.Scan(root, error), error.c_str());
        Require(restarted.ResolveGuid(guid) == moved, "Restart without database lost renamed asset identity");

        const auto relocatedRoot = fixture / "RelocatedAssets";
        std::filesystem::copy(root, relocatedRoot, std::filesystem::copy_options::recursive);
        Require(registry.Scan(relocatedRoot, error), error.c_str());
        Require(registry.ResolveGuid(guid) == relocatedRoot / L"Moved/改名.gltf" && registry.FindByGuid(guid)->cachePath.empty(),
            "Project relocation retained old absolute/cache paths or lost identity");
        const auto database = fixture / "database.json";
        Require(registry.SaveDatabase(database, error), error.c_str());
        AssetRegistry databaseLoaded;
        Require(databaseLoaded.LoadDatabase(database, error) && databaseLoaded.FindByGuid(guid), "Database v3 did not round-trip");
        const auto validDatabase = J::parse(Read(database));
        auto duplicate = validDatabase;
        duplicate["assets"].push_back(duplicate["assets"][0]);
        Write(database, duplicate.dump());
        const auto beforeBadLoad = databaseLoaded.GetAssets().size();
        Require(!databaseLoaded.LoadDatabase(database, error) && databaseLoaded.GetAssets().size() == beforeBadLoad,
            "Duplicate database identities were accepted or mutated existing state");
        auto badNumber = validDatabase;
        badNumber["assets"][0]["size"] = -1.0;
        Write(database, badNumber.dump());
        Require(!databaseLoaded.LoadDatabase(database, error), "Negative asset size accepted");
        Write(database, validDatabase.dump());
        const HANDLE locked = CreateFileW(database.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "Could not lock database fixture");
        const bool overwriteLocked = registry.SaveDatabase(database, error);
        CloseHandle(locked);
        Require(!overwriteLocked && Read(database) == validDatabase.dump(), "Failed atomic database replacement changed existing data");

        Require(registry.Scan(root, error), error.c_str());
        const auto countBeforeCorruption = registry.GetAssets().size();
        const auto movedMetadata = AssetRegistry::MetadataPath(moved);
        const auto goodMetadata = Read(movedMetadata);
        Write(movedMetadata, "{broken");
        Require(!registry.Scan(root, error) && registry.GetAssets().size() == countBeforeCorruption && Read(movedMetadata) == "{broken",
            "Corrupt sidecar was overwritten or failed scan mutated the registry");
        Write(movedMetadata, goodMetadata);
        const auto copied = root / "copied.gltf";
        Write(copied, "model");
        Write(AssetRegistry::MetadataPath(copied), goodMetadata);
        Require(!registry.Scan(root, error), "Copied duplicate GUID was accepted");
        std::filesystem::remove(copied);
        std::filesystem::remove(AssetRegistry::MetadataPath(copied));
        Require(registry.Scan(root, error), error.c_str());
        AssetRecord sentinel;
        sentinel.guid = "untouched";
        const auto outside = fixture / "outside.glb";
        Write(outside, "outside");
        Require(!registry.ImportAsset(outside, fixture / "Cache", sentinel, error) && sentinel.guid == "untouched" &&
            !std::filesystem::exists(AssetRegistry::MetadataPath(outside)), "External import escaped scope or mutated its output");

        const auto legacyRoot = fixture / "Legacy";
        std::filesystem::create_directories(legacyRoot);
        const auto legacySource = legacyRoot / "legacy.obj";
        Write(legacySource, "legacy");
        AssetRegistry legacy;
        Require(legacy.Scan(legacyRoot, error, AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
        const auto legacyGuid = legacy.GetAssets()[0].guid;
        Require(legacyGuid.size() == 16 && !std::filesystem::exists(AssetRegistry::MetadataPath(legacySource)), "Read-only scan created missing metadata");
        Require(legacy.Scan(legacyRoot, error) && legacy.GetAssets()[0].guid == legacyGuid &&
            std::filesystem::exists(AssetRegistry::MetadataPath(legacySource)), "Legacy database/path identity was not preserved during migration");
        const auto renamedLegacy = legacyRoot / "renamed.obj";
        std::filesystem::rename(legacySource, renamedLegacy);
        std::filesystem::rename(AssetRegistry::MetadataPath(legacySource), AssetRegistry::MetadataPath(renamedLegacy));
        Write(legacySource, "legacy replacement");
        Require(legacy.Scan(legacyRoot, error) && legacy.ResolveGuid(legacyGuid) == renamedLegacy &&
            legacy.FindByPath(legacySource)->guid != legacyGuid, "Legacy cached ID hijacked the renamed asset before refresh");
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: persistent GUIDs, Unicode paths, rename/restart/relocation, legacy migration, read-only scan and failure isolation\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(fixture);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
