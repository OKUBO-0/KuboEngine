#pragma once
#include "AssetRegistry.h"
#include "AssetMove.h"
#include "AssetDependencies.h"
#include "AssetImportFingerprint.h"
#include "AssetSceneDependencies.h"
#include "../base/AssetPath.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

namespace Engine::Assets {
// Main-thread owner; worker owns all registry/filesystem work, never live UI/world objects.
class AssetImportJob final {
public:
    enum class Stage { Validate, Scan, Dependencies, Move, Hash, Import, Save, Finished };
    struct Request { std::filesystem::path projectRoot, assetRoot; AssetMoveRequest move; bool recoverMove = false; std::filesystem::path projectFile; };
    struct Result {
        Request request;
        AssetRegistry registry;
        AssetDependencies dependencies;
        std::string error;
        bool success = false, cancelled = false, readOnly = false;
        bool moveCommitted = false, moveRollbackFailed = false;
        bool recoveryCommitted = false;
        size_t imported = 0, skipped = 0;
        bool Matches(const Request& expected) const {
            return request.projectRoot == expected.projectRoot && request.assetRoot == expected.assetRoot && request.projectFile == expected.projectFile;
        }
        bool ApplyTo(AssetRegistry& destination, const Request& expected, AssetDependencies* graph = nullptr) {
            if (!success || cancelled || !Matches(expected)) return false;
            destination = std::move(registry);
            if (graph) *graph = std::move(dependencies);
            success = false; // Completion is consumed once.
            return true;
        }
    };
    struct Progress { Stage stage; size_t completed, total; };
    AssetImportJob() = default;
    AssetImportJob(const AssetImportJob&) = delete;
    AssetImportJob& operator=(const AssetImportJob&) = delete;
    ~AssetImportJob() { CancelAndWait(); }

    bool Start(Request request) {
        if (future_.valid()) return false;
        state_ = std::make_shared<State>();
        state_->moveRequested = request.recoverMove || !request.move.sourceRelative.empty() || !request.move.destinationRelative.empty() || !request.move.guid.empty();
        const auto state = state_;
        try {
            future_ = std::async(std::launch::async, [request = std::move(request), state]() { return Run(request, state); });
            return true;
        } catch (...) { state_.reset(); return false; }
    }
    bool IsBusy() const { return future_.valid(); }
    bool IsMovePending() const { return future_.valid() && state_ && state_->moveRequested; }
    Progress GetProgress() const {
        if (!state_) return {Stage::Finished, 0, 0};
        return {state_->stage.load(), state_->completed.load(), state_->total.load()};
    }
    void Cancel() { if (state_) state_->cancelled.store(true); }
    bool Poll(Result& result) {
        if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        result = future_.get();
        if (state_->cancelled.load()) { result.cancelled = true; result.success = false; }
        state_.reset();
        return true;
    }
    void CancelAndWait() noexcept {
        Cancel();
        if (future_.valid()) { try { future_.get(); } catch (...) {} }
        state_.reset();
    }
private:
    struct State {
        bool moveRequested = false;
        std::atomic<bool> cancelled{false};
        std::atomic<Stage> stage{Stage::Validate};
        std::atomic<size_t> completed{0}, total{0};
    };
    static bool Inside(const std::filesystem::path& path, const std::filesystem::path& root) {
        const auto canonical = std::filesystem::weakly_canonical(path);
        auto part = canonical.begin();
        for (const auto& component : root) {
            if (part == canonical.end() || CompareStringOrdinal(component.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
            ++part;
        }
        return true;
    }
    static Result Run(const Request& request, const std::shared_ptr<State>& state) {
        Result result; result.request = request;
        const auto check = [&]() { if (state->cancelled.load()) throw std::runtime_error("Asset import cancelled"); };
        try {
            check();
            if (request.projectRoot.empty() || request.assetRoot.empty() || !request.projectRoot.is_absolute() || !request.assetRoot.is_absolute())
                throw std::runtime_error("Asset job requires absolute project/asset roots");
            const auto project = std::filesystem::canonical(request.projectRoot);
            const auto database = project / "Library/asset_registry.json";
            const auto legacy = project / ".kubo/asset_registry.json";
            const auto cache = project / "Library/Imported";
            if (!Inside(database, project) || !Inside(cache, project) || !Inside(legacy, project))
                throw std::runtime_error("Asset database/cache resolves outside the project");
            result.readOnly = !Inside(request.assetRoot, project);
            const bool moving = !request.move.sourceRelative.empty() || !request.move.destinationRelative.empty() || !request.move.guid.empty();
            if (request.recoverMove) {
                if (result.readOnly || moving) throw std::runtime_error("Recovery requires a project-local root and no new move request");
                check(); state->stage.store(Stage::Move);
                const auto recovered = RecoverAssetMove(request.assetRoot, [&]() { return state->cancelled.load(); });
                result.recoveryCommitted = recovered.success;
                if (!recovered.success) throw std::runtime_error(recovered.error);
            }
            if (moving) {
                if (result.readOnly) throw std::runtime_error("External assets cannot be moved by this project");
                if (request.move.sourceRelative.empty() || request.move.destinationRelative.empty() || request.move.guid.empty())
                    throw std::runtime_error("Move requires source, destination and selected GUID");
                MoveDetail::Relative(request.move.sourceRelative); MoveDetail::Relative(request.move.destinationRelative);
            }
            const auto existing = std::filesystem::is_regular_file(database) ? database : legacy;
            if (std::filesystem::is_regular_file(existing) && !result.registry.LoadDatabase(existing, result.error))
                throw std::runtime_error(result.error);
            check(); state->stage.store(Stage::Scan);
            if (!result.registry.Scan(request.assetRoot, result.error, (result.readOnly || moving) ? AssetRegistry::MetadataPolicy::ReadOnly
                : AssetRegistry::MetadataPolicy::CreateMissing, [&]() { return state->cancelled.load(); })) throw std::runtime_error(result.error);
            if (moving) {
                const auto* selected = result.registry.FindByGuid(request.move.guid);
                if (!selected || selected->relativePath != request.move.sourceRelative) throw std::runtime_error("Move selection is stale; refresh and select the asset again");
                check(); state->stage.store(Stage::Dependencies);
                result.dependencies = InspectAssetDependencies(result.registry, [&]() { return state->cancelled.load(); });
                AppendProjectAssetDependencies(result.dependencies,result.registry,project,request.projectFile,[&](){return state->cancelled.load();});
                const auto risk = result.dependencies.MoveRisk(result.registry, request.move.guid, request.move.destinationRelative);
                if (!risk.empty()) throw std::runtime_error(risk);
                check(); state->stage.store(Stage::Move);
                const auto moved = MoveAssetPair(request.assetRoot, request.move, [&]() { return state->cancelled.load(); });
                result.moveCommitted = moved.success; result.moveRollbackFailed = moved.rollbackFailed;
                if (!moved.success) throw std::runtime_error(moved.error);
                check();
                if (!result.registry.Scan(request.assetRoot, result.error, AssetRegistry::MetadataPolicy::CreateMissing,
                    [&]() { return state->cancelled.load(); })) throw std::runtime_error(result.error);
            }
            check(); state->stage.store(Stage::Dependencies);
            result.dependencies = InspectAssetDependencies(result.registry, [&]() { return state->cancelled.load(); });
            AppendProjectAssetDependencies(result.dependencies,result.registry,project,request.projectFile,[&](){return state->cancelled.load();});
            check(); state->stage.store(Stage::Hash);
            state->completed.store(0); state->total.store(result.registry.GetAssets().size());
            const auto fingerprints = BuildAssetImportFingerprints(result.registry, result.dependencies,
                [&]() { return state->cancelled.load(); }, [&]() { state->completed.fetch_add(1); });
            for (const auto& fingerprint : fingerprints) {
                check();
                if (!result.registry.SetImportFingerprint(fingerprint.first, fingerprint.second)) throw std::runtime_error("Cannot assign asset import fingerprint");
            }
            state->stage.store(Stage::Import);
            state->completed.store(0);
            const auto sources = result.registry.GetAssets();
            state->total.store(sources.size());
            for (const auto& source : sources) {
                check();
                if (result.readOnly || result.registry.IsImportCurrent(source, cache)) ++result.skipped;
                else {
                    if (!Inside(cache, project)) throw std::runtime_error("Asset cache moved outside the project");
                    AssetRecord imported;
                    if (!result.registry.ImportAsset(Base::PathFromUtf8(source.absolutePath), cache, imported, result.error, source.importFingerprint)) throw std::runtime_error(result.error);
                    ++result.imported;
                }
                state->completed.fetch_add(1);
            }
            check(); state->stage.store(Stage::Save);
            if (!Inside(database, project)) throw std::runtime_error("Asset database moved outside the project");
            if (!result.registry.SaveDatabase(database, result.error)) throw std::runtime_error(result.error);
            check(); result.success = true;
        } catch (const std::exception& exception) { result.error = exception.what(); }
        result.cancelled = state->cancelled.load();
        state->stage.store(Stage::Finished);
        return result;
    }
    std::shared_ptr<State> state_;
    std::future<Result> future_;
};
}
