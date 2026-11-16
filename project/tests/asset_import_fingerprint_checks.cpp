#include "../engine/assets/AssetImportJob.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace {
namespace fs = std::filesystem;
using namespace Engine::Assets;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const fs::path& path, const std::string& bytes) { std::ofstream file(path, std::ios::binary); file << bytes; Require(bool(file), "Fixture write failed"); }
std::string Read(const fs::path& path) { std::ifstream file(path, std::ios::binary); Require(bool(file), "Fixture read failed"); return {std::istreambuf_iterator<char>(file), {}}; }
AssetImportJob::Result Wait(AssetImportJob& job) {
    AssetImportJob::Result result; const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!job.Poll(result)) { Require(std::chrono::steady_clock::now() < limit, "Fingerprint worker timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return result;
}
}
int main() {
    const auto fixture = fs::temp_directory_path() / (L"kubo-import-hash-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto project = fixture / L"日本語", root = project / "Assets";
        fs::create_directories(root);
        const auto obj = root / "model.obj", mtl = root / L"材質.mtl", image = root / L"色.png", unrelated = root / "other.txt";
        Write(obj, "mtllib 材質.mtl\n"); Write(mtl, "map_Kd 色.png\n"); Write(image, "abc"); Write(unrelated, "unrelated");
        Require(HashAssetContent(image) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "Actual SHA-256 known vector failed");
        Write(image, "");
        Require(HashAssetContent(image) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty SHA-256 vector failed");
        Write(image, "abc");
        bool cancelled = false;
        try { HashAssetContent(image, [] { return true; }); } catch (...) { cancelled = true; }
        Require(cancelled, "Content hash cancellation ignored");
        AssetImportJob job; Require(job.Start({project, root}), "Initial hash refresh start failed");
        auto result = Wait(job); Require(result.success && result.imported == 4, result.error.c_str());
        AssetRegistry registry; Require(result.ApplyTo(registry, {project, root}), "Initial publication failed");
        const auto objGuid = registry.FindByPath(obj)->guid, mtlGuid = registry.FindByPath(mtl)->guid, imageGuid = registry.FindByPath(image)->guid;
        const auto oldObj = registry.FindByGuid(objGuid)->importFingerprint, oldMtl = registry.FindByGuid(mtlGuid)->importFingerprint;
        Require(oldObj.size() == 64 && oldMtl.size() == 64, "Composite fingerprints missing from registry");
        AssetRegistry restored; std::string error;
        Require(restored.LoadDatabase(project / "Library/asset_registry.json", error) && restored.FindByGuid(objGuid)->importFingerprint == oldObj,
            "Composite fingerprint did not persist in database");
        const auto cachePath = Engine::Base::PathFromUtf8(registry.FindByGuid(objGuid)->cachePath);
        const auto database = project / "Library/asset_registry.json";
        auto legacyDatabase = crude_json::value::parse(Read(database));
        auto invalidDatabase = legacyDatabase;
        invalidDatabase["assets"][0]["importFingerprint"] = "invalid";
        const auto invalidPath = fixture / "invalid-database.json";
        Write(invalidPath, invalidDatabase.dump());
        Require(!restored.LoadDatabase(invalidPath, error) && restored.FindByGuid(objGuid)->importFingerprint == oldObj, "Malformed digest loaded or changed existing database state");
        for (auto& record : legacyDatabase["assets"].get<crude_json::array>()) record.erase("importFingerprint");
        Write(database, legacyDatabase.dump());
        auto legacyCache = crude_json::value::parse(Read(cachePath)); legacyCache.erase("importFingerprint"); Write(cachePath, legacyCache.dump());
        AssetRegistry legacyLoaded;
        Require(legacyLoaded.LoadDatabase(database, error) && legacyLoaded.FindByGuid(objGuid)->importFingerprint.empty(), "Legacy database without digest no longer loads");
        Require(job.Start({project, root}), "Legacy cache refresh start failed"); result = Wait(job);
        Require(result.success && result.imported == 1 && result.skipped == 3, "Legacy cache did not upgrade exactly once");
        const auto unchangedTime = fs::last_write_time(cachePath);
        Require(job.Start({project, root}), "Unchanged refresh start failed"); result = Wait(job);
        Require(result.success && result.imported == 0 && result.skipped == 4 && fs::last_write_time(cachePath) == unchangedTime, "Unchanged hashes rewrote caches");
        const auto imageTime = fs::last_write_time(image);
        Write(image, "xyz"); fs::last_write_time(image, imageTime);
        Require(job.Start({project, root}), "Same-time content refresh start failed"); result = Wait(job);
        Require(result.success && result.imported == 3 && result.skipped == 1 && result.registry.FindByGuid(objGuid)->importFingerprint != oldObj &&
            result.registry.FindByGuid(mtlGuid)->importFingerprint != oldMtl && result.registry.FindByGuid(imageGuid), "Same-size/time texture edit did not invalidate transitive owners");
        Require(result.ApplyTo(registry, {project, root}), "Changed fingerprint publication failed");
        fs::remove(image); fs::remove(AssetRegistry::MetadataPath(image));
        Require(job.Start({project, root}), "Missing dependency refresh start failed"); result = Wait(job);
        Require(result.success && result.imported == 2 && result.skipped == 1 && result.dependencies.DependenciesOf(mtlGuid)[0]->missing,
            "Missing texture did not invalidate owners or unrelated asset was reimported");
        Write(image, "xyz");
        Require(job.Start({project, root}), "Dependency reappearance refresh start failed"); result = Wait(job);
        Require(result.success && result.imported == 3 && result.skipped == 1, "Reappearing dependency did not invalidate owners");
        // Cycles are represented deterministically by an iterative closure.
        auto graph = result.dependencies;
        const auto newImageGuid = result.registry.FindByPath(image)->guid;
        graph.edges.push_back({newImageGuid, objGuid, "model.obj", "model.obj", false});
        const auto first = BuildAssetImportFingerprints(result.registry, graph);
        std::reverse(graph.edges.begin(), graph.edges.end());
        const auto second = BuildAssetImportFingerprints(result.registry, graph);
        Require(first == second && first.size() == 4, "Cyclic fingerprints did not terminate or depend on edge order");
        Write(image, std::string(150000, 'a'));
        FingerprintDetail::Hash direct; direct.Open(); const std::string bytes(150000, 'a');
        direct.Add(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<ULONG>(bytes.size()));
        Require(HashAssetContent(image) == direct.Finish(), "Chunked hash lost bytes");
        int checkpoints = 0; cancelled = false;
        try { HashAssetContent(image, [&] { return ++checkpoints == 3; }); } catch (...) { cancelled = true; }
        Require(cancelled && checkpoints == 3, "Chunk-time hash cancellation ignored");
        Require(!registry.SetImportFingerprint(objGuid, "invalid") && !registry.SetImportFingerprint(std::string(32, 'f'), std::string(64, 'a')),
            "Invalid fingerprint assignment mutated registry");
        AssetRecord legacyImport;
        Require(registry.ImportAsset(obj, project / "LegacyImport", legacyImport, error) && legacyImport.importFingerprint.empty(),
            "Legacy direct importer claimed a stale composite digest");
        {
            HANDLE lock = CreateFileW(image.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(lock != INVALID_HANDLE_VALUE, "Writer lock fixture failed");
            bool rejected = false;
            try { HashAssetContent(image); } catch (...) { rejected = true; }
            CloseHandle(lock); Require(rejected, "Hash ignored active writer lock");
        }
        fs::remove_all(fixture);
        std::cout << "PASS: SHA-256 content/composite persistence, same-size/time edits, transitive invalidation, missing/reappearing dependencies, cycles and unchanged skipping\n";
        return 0;
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; std::error_code ec; fs::remove_all(fixture, ec); return 1; }
}
