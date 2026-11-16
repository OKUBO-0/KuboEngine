#include "engine/camera/CameraManager.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <imgui.h>

// MyMath's UI link dependencies must never run in this CPU-only fixture.
namespace ImGui {
void Text(const char*, ...) { throw std::runtime_error("Unexpected Camera CPU UI call"); }
void SameLine(float, float) { throw std::runtime_error("Unexpected Camera CPU UI call"); }
}

namespace { bool failNext = false; unsigned faultCount = 0; }
void* operator new(size_t size) {
    if (failNext) { failNext = false; ++faultCount; throw std::bad_alloc(); }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }

using Engine::CameraSystem::Camera;
using Engine::CameraSystem::CameraManager;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Reset {
    ~Reset() { failNext = false; CameraManager::GetInstance()->Finalize(); }
};
template<class Action> void AllocationFree(Action action) {
    const auto before = faultCount;
    failNext = true;
    bool result = false;
    try { result = action(); }
    catch (...) { failNext = false; throw; }
    const bool unconsumed = failNext && faultCount == before;
    failNext = false;
    Require(result && unconsumed, "Shutdown path allocated or violated its contract");
}
}
int main() {
    try {
        Camera own, editor, previous, replacement, unrelated;
        Reset cleanup; // Remove borrowed registrations before stack cameras die.
        auto* manager = CameraManager::GetInstance();
        manager->Initialize();
        const std::string authored = "__entity_scene_camera";
        const std::string editorName = "__entity_scene_editor_camera";
        const std::string prior(512, 'p');
        manager->AddCamera(prior, &previous);
        manager->AddCamera(authored, &own);
        manager->SetActiveCamera(authored);
        AllocationFree([&] {
            return manager->FindCamera("__entity_scene_camera") == &own &&
                manager->DetachCamera("__entity_scene_camera", &own, prior) &&
                manager->GetActiveCamera() == &previous &&
                std::string_view(manager->GetActiveCameraName()) == prior &&
                !manager->FindCamera(authored);
        });
        AllocationFree([&] { return !manager->DetachCamera(authored, &own, prior); });

        manager->AddCamera(authored, &own);
        manager->AddCamera(authored, &replacement);
        manager->SetActiveCamera(authored);
        AllocationFree([&] {
            return !manager->DetachCamera(authored, &own, prior) &&
                !manager->DetachCamera(authored, nullptr, prior) &&
                manager->GetActiveCamera() == &replacement;
        });
        manager->AddCamera("unrelated", &unrelated);
        manager->SetActiveCamera("unrelated");
        AllocationFree([&] {
            return manager->DetachCamera(authored, &replacement, prior) &&
                manager->GetActiveCamera() == &unrelated;
        });

        // Both scene registrations: a preferred fallback can itself be detached.
        manager->AddCamera(authored, &own);
        manager->AddCamera(editorName, &editor);
        manager->SetActiveCamera(editorName);
        AllocationFree([&] {
            return manager->DetachCamera(editorName, &editor, authored) &&
                manager->GetActiveCamera() == &own &&
                manager->DetachCamera(authored, &own, authored) &&
                manager->GetActiveCamera() == manager->FindCamera("default");
        });

        manager->SetActiveCamera(prior);
        for (unsigned index = 0; index < 256; ++index)
            manager->AddCamera("rehash-" + std::to_string(index), &unrelated);
        AllocationFree([&] {
            return manager->GetActiveCamera() == &previous &&
                std::string_view(manager->GetActiveCameraName()) == prior;
        });
        // View aliases the erased key; fallback is resolved before destruction.
        manager->AddCamera(authored, &own);
        manager->SetActiveCamera(authored);
        AllocationFree([&] {
            const std::string_view key = manager->GetActiveCameraName();
            return manager->DetachCamera(key, &own, key) &&
                manager->GetActiveCamera() == manager->FindCamera("default");
        });
        manager->SetActiveCamera(prior);
        AllocationFree([&] {
            manager->RemoveCamera(manager->GetActiveCameraName());
            return !manager->FindCamera(prior) && manager->GetActiveCamera() != nullptr;
        });

        AllocationFree([&] { manager->Finalize(); manager->Finalize();
            return !manager->GetActiveCamera() && manager->GetActiveCameraName().empty(); });
        // No default: do not silently choose an unrelated registration on detach.
        manager->AddCamera(authored, &own);
        manager->AddCamera("other", &unrelated);
        manager->SetActiveCamera(authored);
        AllocationFree([&] {
            return manager->DetachCamera(authored, &own, "missing") &&
                !manager->GetActiveCamera() && manager->GetActiveCameraName().empty() &&
                manager->FindCamera("other") == &unrelated;
        });
        // Manager-first teardown and later borrowed-owner cleanup are idempotent.
        AllocationFree([&] { manager->Finalize();
            return !manager->DetachCamera(authored, &own, prior) && !manager->GetActiveCamera(); });
        manager->Initialize();
        Require(manager->GetActiveCamera() == manager->FindCamera("default"), "Next host did not recover");
        std::cout << "PASS: camera identity detach, long-name restore, replacement/unrelated preservation, pair cleanup, rehash/alias, no-default and repeated/manager-first shutdown\n"
            << "CAMERA_SHUTDOWN_FAULT consumed=" << faultCount
            << " (armed ordinary faults stayed unconsumed on allocation-free paths; no GPU/window initialized)\n";
        return 0;
    } catch (const std::exception& error) {
        failNext = false; std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
