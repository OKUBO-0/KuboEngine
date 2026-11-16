#include "../engine/assets/AssetImportJob.h"
#include "../engine/assets/ModelReference.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace {
void Require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& bytes) { std::ofstream output(path, std::ios::binary); output << bytes; Require(bool(output), "Fixture write failed"); }
std::string Read(const std::filesystem::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}; }
Engine::Assets::AssetImportJob::Result Wait(Engine::Assets::AssetImportJob& job) {
    Engine::Assets::AssetImportJob::Result result;
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.Poll(result)) { Require(std::chrono::steady_clock::now() < timeout, "Move worker timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return result;
}
}
int main() {
    namespace fs = std::filesystem;
    using namespace Engine;
    using namespace Engine::Assets;
    const auto fixture = fs::temp_directory_path() / (L"kubo-asset-move-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto project = fixture / L"日本語"; const auto root = project / "Assets";
        fs::create_directories(root / "Models"); fs::create_directories(root / L"移動先");
        const auto source = root / L"Models/モデル.obj";
        Write(source, "original model");
        AssetRegistry registry; std::string error;
        Require(registry.Scan(root, error), error.c_str());
        const auto guid = registry.FindByPath(source)->guid;
        const auto metadataBytes = Read(AssetRegistry::MetadataPath(source));
        const std::string from = "Models/モデル.obj", target = "移動先/改名.obj";
        Scene::EntityWorld world; const auto id = world.CreateEntity("Model"); world.SetModel(id, "Assets/Models/モデル.obj", guid);
        for (const auto& bad : {"../escape.obj", "Models/../../escape.obj", "C:/escape.obj", "/absolute.obj", "Models/NUL.obj", "Models/new.obj ", "Models/change.gltf", "Library/new.obj", "Models/new.kubo-meta.json", "Missing/new.obj"}) {
            auto moved = MoveAssetPair(root, {from, bad, guid});
            Require(!moved.success && fs::is_regular_file(source) && Read(AssetRegistry::MetadataPath(source)) == metadataBytes, "Invalid destination changed source");
        }
        Require(!MoveAssetPair(root, {from, target, std::string(32, 'f')}).success, "Stale GUID accepted");
        const auto dest = root / Base::PathFromUtf8(target);
        Write(dest, "destination owned by somebody else");
        Require(!MoveAssetPair(root, {from, target, guid}).success && Read(dest) == "destination owned by somebody else", "Destination overwritten");
        fs::remove(dest);
        // Deterministically introduce a competing metadata destination after the first rename.
        int checks = 0;
        auto moved = MoveAssetPair(root, {from, target, guid}, [&]() {
            if (++checks == 3) Write(AssetRegistry::MetadataPath(dest), "competing metadata");
            return false;
        });
        Require(!moved.success && !moved.rollbackFailed && fs::is_regular_file(source) && !fs::exists(dest) &&
            Read(AssetRegistry::MetadataPath(source)) == metadataBytes && Read(AssetRegistry::MetadataPath(dest)) == "competing metadata", "Second-rename failure did not restore source or overwrote competitor");
        fs::remove(AssetRegistry::MetadataPath(dest));
        checks = 0;
        moved = MoveAssetPair(root, {from, target, guid}, [&]() {
            if (++checks == 3) { Write(source, "competing source"); Write(AssetRegistry::MetadataPath(dest), "competing metadata"); }
            return false;
        });
        Require(!moved.success && moved.rollbackFailed && Read(source) == "competing source" && Read(dest) == "original model" &&
            Read(AssetRegistry::MetadataPath(source)) == metadataBytes && moved.error.find("rollback failed") != std::string::npos,
            "Rollback overwrote competing source or failed to report recoverable paths");
        const auto journal = root / ".kubo/asset-move.pending.json";
        Require(fs::is_regular_file(journal) && !registry.Scan(root, error) &&
            ResolveModelReference(world.Find(id)->model, registry, project, error).empty(), "Pending recovery allowed wrong-file GUID resolution");
        const auto journalBytes = Read(journal);
        Require(!RecoverAssetMove(root).success && Read(source) == "competing source" && Read(dest) == "original model" &&
            Read(journal) == journalBytes, "Recovery changed a competing file or cleared its guard");
        // Only remove competing files created by this test; production recovery
        // must inspect the persisted file identities and restore the intact pair.
        fs::remove(source); fs::remove(AssetRegistry::MetadataPath(dest));
        Require(!RecoverAssetMove(root, [] { return true; }).success && fs::exists(journal) && fs::exists(dest), "Cancelled recovery mutated files");
        checks = 0;
        moved = RecoverAssetMove(root, [&] {
            if (++checks == 2) Write(source, "racing recovery competitor");
            return false;
        });
        Require(!moved.success && fs::exists(journal) && Read(source) == "racing recovery competitor" && Read(dest) == "original model",
            "Recovery rollback overwrote a racing creator or cleared its journal");
        fs::remove(source);
        moved = RecoverAssetMove(root);
        Require(moved.success && !fs::exists(journal) && Read(source) == "original model" &&
            Read(AssetRegistry::MetadataPath(source)) == metadataBytes && !fs::exists(dest), moved.error.c_str());
        Write(journal, journalBytes);
        AssetImportJob recoveryJob;
        Require(recoveryJob.Start({project, root, {}, true}) && recoveryJob.IsMovePending(), "Recovery worker did not start with playback lock");
        auto recoveredResult = Wait(recoveryJob);
        Require(recoveredResult.success && recoveredResult.recoveryCommitted && recoveredResult.ApplyTo(registry, {project, root}) &&
            !fs::exists(journal) && Read(source) == "original model" && registry.ResolveGuid(guid) == source, "Intact original pair worker recovery/publication failed");
        Write(journal, "{\"version\":1}");
        Require(!RecoverAssetMove(root).success && fs::exists(journal), "Legacy journal cleared without file identities");
        Write(journal, journalBytes);
        checks = 0;
        Require(!RecoverAssetMove(root, [&] { return ++checks == 2; }).success && fs::exists(journal) &&
            fs::exists(source), "Recovery cancellation after identity inspection cleared journal");
        auto invalidJournal = crude_json::value::parse(journalBytes);
        invalidJournal["sourceRelative"] = "../escape.obj";
        Write(journal, invalidJournal.dump());
        Require(!RecoverAssetMove(root).success && fs::exists(journal) && Read(source) == "original model", "Recovery journal escaped root");
        Write(journal, journalBytes);
        {
            MoveDetail::File lock;
            Require(lock.Open(journal), "Recovery lock fixture failed");
            Require(!RecoverAssetMove(root).success && fs::exists(journal), "Recovery ignored competing journal handle");
        }
        Write(AssetRegistry::MetadataPath(source), "{\"version\":1,\"guid\":\"" + std::string(32, 'e') + "\"}");
        Require(!RecoverAssetMove(root).success && fs::exists(journal), "Recovery accepted mutated GUID in original sidecar");
        Write(AssetRegistry::MetadataPath(source), metadataBytes);
        const auto originalSidecar = AssetRegistry::MetadataPath(source);
        const auto heldSidecar = root / "held-sidecar";
        fs::rename(originalSidecar, heldSidecar); Write(originalSidecar, metadataBytes);
        Require(!RecoverAssetMove(root).success && fs::exists(journal) && Read(originalSidecar) == metadataBytes,
            "Same-GUID replaced sidecar accepted without original file identity");
        fs::remove(originalSidecar); fs::rename(heldSidecar, originalSidecar);
        Require(RecoverAssetMove(root).success, "Repaired original sidecar recovery failed");
        {
            MoveDetail::File lock;
            lock.handle = CreateFileW(AssetRegistry::MetadataPath(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(lock.handle != INVALID_HANDLE_VALUE, "Metadata lock fixture failed");
            moved = MoveAssetPair(root, {from, target, guid});
            Require(!moved.success && !moved.rollbackFailed && fs::is_regular_file(source) && !fs::exists(dest), "Locked metadata allowed a partial move");
        }
        checks = 0;
        moved = MoveAssetPair(root, {from, target, guid}, [&]() { return ++checks == 3; });
        Require(!moved.success && !moved.rollbackFailed && fs::is_regular_file(source) && !fs::exists(dest), "Mid-pair cancellation did not roll back");
        AssetImportJob job;
        Require(job.Start({project, root, {from, target, guid}}), "Move job did not start");
        Require(job.IsMovePending(), "Move job did not lock playback while awaiting completion");
        auto result = Wait(job);
        Require(result.success && result.moveCommitted, result.error.c_str());
        Require(!fs::exists(journal), "Successful move left a pending recovery guard");
        Require(!job.IsMovePending(), "Consumed move left playback locked");
        Require(result.ApplyTo(registry, {project, root}), "Move result did not publish");
        Require(!fs::exists(source) && !fs::exists(AssetRegistry::MetadataPath(source)) && fs::is_regular_file(dest) &&
            Read(AssetRegistry::MetadataPath(dest)) == metadataBytes && registry.ResolveGuid(guid) == dest, "Move lost paired GUID or registry mapping");
        Require(ResolveModelReference(world.Find(id)->model, registry, project, error) == dest, "Authored GUID reference did not follow move");
        // A crash after both renames but before marker removal: acknowledge the
        // verified destination pair without moving it back or changing bytes.
        Write(journal, journalBytes);
        Require(RecoverAssetMove(root).success && !fs::exists(journal) && Read(dest) == "original model" &&
            Read(AssetRegistry::MetadataPath(dest)) == metadataBytes, "Committed pair recovery changed destination");
        Require(job.Start({project, root, {from, "Models/wrong.obj", guid}}), "Stale move job start failed");
        result = Wait(job); Require(!result.success && !result.moveCommitted && fs::exists(dest), "Stale UI path moved the current GUID asset");
        const auto legacySource = root / "Models/legacy.obj";
        const std::string legacyGuid = "0123456789abcdef";
        Write(legacySource, "legacy model");
        Write(AssetRegistry::MetadataPath(legacySource), "{\"version\":1,\"guid\":\"" + legacyGuid + "\"}");
        Require(job.Start({project, root, {"Models/legacy.obj", "Models/renamed-legacy.obj", legacyGuid}}), "Legacy GUID move start failed");
        result = Wait(job);
        Require(result.success && result.moveCommitted && result.registry.ResolveGuid(legacyGuid) == root / "Models/renamed-legacy.obj", "Legacy persistent GUID was lost in move");
        const auto external = fixture / "External"; fs::create_directories(external); Write(external / "outside.obj", "outside");
        AssetRegistry externalRegistry; Require(externalRegistry.Scan(external, error), error.c_str());
        const auto externalGuid = externalRegistry.FindByPath(external / "outside.obj")->guid;
        Require(job.Start({project, external, {"outside.obj", "renamed.obj", externalGuid}}), "External move start failed");
        result = Wait(job); Require(!result.success && !result.moveCommitted && fs::is_regular_file(external / "outside.obj"), "Project moved external source");
        Require(job.Start({project, external, {}, true}), "External recovery request start failed");
        result = Wait(job); Require(!result.success && !result.recoveryCommitted && fs::is_regular_file(external / "outside.obj"), "Recovery mutated external root");
        fs::remove_all(fixture);
        std::cout << "PASS: paired asset move/recovery, held file identities, GUID references, competitor preservation, cancellation and background publication\n";
        return 0;
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; std::error_code ec; fs::remove_all(fixture, ec); return 1; }
}
