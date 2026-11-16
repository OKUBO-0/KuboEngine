#include "../engine/assets/AssetImportJob.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary); output << bytes; Require(bool(output), "Fixture write failed");
}
Engine::Assets::AssetImportJob::Result Wait(Engine::Assets::AssetImportJob& job) {
    Engine::Assets::AssetImportJob::Result result;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.Poll(result)) {
        Require(std::chrono::steady_clock::now() < until, "Asset worker timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result;
}
}
int main() {
    namespace fs = std::filesystem;
    using namespace Engine::Assets;
    const auto fixture = fs::temp_directory_path() / (L"kubo-asset-worker-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto project = fixture / L"Project_日本語";
        const auto root = project / "Content";
        fs::create_directories(root);
        Write(root / L"モデル.obj", "model");
        Write(root / "mover.kubo", "on_update translate 1 0 0\n");
        AssetImportJob job;
        Require(job.Start({project, root}), "Worker start failed");
        Require(!job.Start({project, root}), "Concurrent job accepted");
        auto result = Wait(job);
        Require(result.success && result.imported == 2 && result.skipped == 0 && !result.readOnly, result.error.c_str());
        Require(!job.IsBusy(), "Completed future remained busy");
        AssetRegistry published;
        Require(!result.ApplyTo(published, {project, project / "OtherRoot"}) && published.GetAssets().empty(), "Stale root completion was published");
        Require(result.ApplyTo(published, {project, root}), "Valid completion was not published");
        Require(!result.ApplyTo(published, {project, root}), "Completion could be applied twice");
        const auto guid = published.FindByPath(root / L"モデル.obj")->guid;
        const auto cache = Engine::Base::PathFromUtf8(published.FindByGuid(guid)->cachePath);
        const auto cacheTime = fs::last_write_time(cache);
        Require(job.Start({project, root}), "Second refresh start failed");
        result = Wait(job);
        Require(result.success && result.imported == 0 && result.skipped == 2, "Unchanged assets were reimported");
        Require(fs::last_write_time(cache) == cacheTime, "Unchanged cache was rewritten");
        Write(root / L"モデル.obj", "changed model with different size");
        Require(job.Start({project, root}), "Changed refresh start failed");
        result = Wait(job);
        Require(result.success && result.imported == 1 && result.skipped == 1 && result.registry.FindByGuid(guid), "Changed asset did not reimport with stable GUID");
        published = std::move(result.registry);
        Write(cache, "corrupt");
        Require(job.Start({project, root}), "Cache repair start failed");
        result = Wait(job);
        Require(result.success && result.imported == 1, "Invalid import metadata was treated as current");
        // Failure result never mutates the separate live registry or overwrites invalid identities.
        const auto sidecar = AssetRegistry::MetadataPath(root / "mover.kubo");
        Write(sidecar, "invalid sidecar");
        const auto liveCount = published.GetAssets().size();
        Require(job.Start({project, root}), "Failure job start failed");
        result = Wait(job);
        Require(!result.success && !result.cancelled && !result.error.empty() && published.GetAssets().size() == liveCount && published.FindByGuid(guid), "Failure changed published registry");
        // Cancellation is honored even if the tiny worker finishes before Poll.
        Require(job.Start({project, root}), "Cancel start failed"); job.Cancel();
        result = Wait(job);
        Require(result.cancelled && !result.success, "Cancelled completion was publishable");
        Require(!result.ApplyTo(published, {project, root}) && published.FindByGuid(guid), "Cancelled result changed live registry");
        AssetRegistry cancelledScan;
        std::string error;
        Require(!cancelledScan.Scan(root, error, AssetRegistry::MetadataPolicy::CreateMissing, []() { return true; }) && cancelledScan.GetAssets().empty(), "Cancelled scan committed state");
        const auto cancellationRoot = fixture / "CancelDuringScan"; fs::create_directories(cancellationRoot);
        for (int i = 0; i < 5; ++i) Write(cancellationRoot / (std::to_string(i) + ".obj"), "model");
        cancelledScan = published;
        int checkpoints = 0;
        Require(!cancelledScan.Scan(cancellationRoot, error, AssetRegistry::MetadataPolicy::CreateMissing, [&]() { return ++checkpoints >= 3; }) &&
            checkpoints == 3 && cancelledScan.FindByGuid(guid) && cancelledScan.GetAssets().size() == liveCount,
            "Cooperative enumeration cancellation changed the live registry");
        Require(!fs::exists(AssetRegistry::MetadataPath(cancellationRoot / "0.obj")), "Cancelled enumeration created identities");
        const auto external = fixture / "External";
        fs::create_directories(external); Write(external / "external.obj", "external");
        const auto externalProject = fixture / "ExternalProject"; fs::create_directories(externalProject);
        Require(job.Start({externalProject, external}), "External job start failed");
        result = Wait(job);
        Require(result.success && result.readOnly && result.imported == 0 && result.skipped == 1 &&
            !fs::exists(AssetRegistry::MetadataPath(external / "external.obj")) && !fs::exists(externalProject / "Library/Imported"), "External source was modified/imported");
        const auto badProject = fixture / "BadDatabase"; fs::create_directories(badProject / "Assets");
        fs::create_directories(badProject / "Library"); Write(badProject / "Assets/new.obj", "new");
        Write(badProject / "Library/asset_registry.json", "invalid database");
        Require(job.Start({badProject, badProject / "Assets"}), "Invalid database job start failed");
        result = Wait(job);
        Require(!result.success && !fs::exists(AssetRegistry::MetadataPath(badProject / "Assets/new.obj")), "Invalid database was hidden by bootstrap scan");
        Require(job.Start({externalProject, external}), "Shutdown start failed"); job.CancelAndWait();
        Require(!job.IsBusy(), "Shutdown left worker alive");
        const auto importProject = fixture / "ImportProject";
        const auto importRoot = importProject / "Assets";
        const auto newFolder = importRoot / L"素材";
        const auto externalModel = external / L"新規.obj";
        const auto externalTexture = external / L"色.png";
        fs::create_directories(newFolder);
        Write(externalModel, "source model bytes");
        Write(externalTexture, "source texture bytes");
        AssetImportJob::Request request{importProject,importRoot};
        request.sourceFiles = {externalModel,externalTexture};
        request.targetFolder = "素材";
        Require(job.Start(request), "External file import start failed");
        result = Wait(job);
        Require(result.success && result.copied == 2 &&
            result.registry.FindByPath(newFolder / externalModel.filename()) &&
            result.registry.FindByPath(newFolder / externalTexture.filename()), result.error.c_str());
        Require(fs::file_size(newFolder / externalModel.filename()) == fs::file_size(externalModel) &&
            !fs::exists(AssetRegistry::MetadataPath(externalModel)) &&
            fs::is_empty(importRoot / ".kubo"), "Import changed a source or left staging data");
        const auto untouched = external / "untouched.obj";
        Write(untouched,"other source");
        request.sourceFiles = {untouched,externalModel};
        Require(job.Start(request), "Collision probe start failed");
        result = Wait(job);
        Require(!result.success && result.copied == 0 && !fs::exists(newFolder / untouched.filename()),
            "Destination conflict accepted a partial selection");
        request.sourceFiles = {untouched};
        request.targetFolder = "../Escape";
        Require(job.Start(request), "Traversal probe start failed");
        result = Wait(job);
        Require(!result.success && result.copied == 0 && !fs::exists(importProject / "Escape"),
            "Import accepted an escaping folder");
        request.targetFolder = "素材";
        request.assetRoot = external;
        Require(job.Start(request), "External destination probe start failed");
        result = Wait(job);
        Require(!result.success && result.copied == 0, "Import copied into a read-only root");
        fs::remove_all(fixture);
        std::cout << "PASS: async asset scan/import, incremental cache, cancellation, failure isolation, external roots, Japanese file intake and collision/traversal refusal\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n'; std::error_code error; fs::remove_all(fixture, error); return 1;
    }
}
