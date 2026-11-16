#include "CameraManager.h"
#include <cassert>
#include <Logger.h>

namespace Engine::CameraSystem {

CameraManager* CameraManager::GetInstance()
{
    static CameraManager manager;
    return &manager;

}

void CameraManager::Finalize()
{
    activeCameraName = nullptr;
    cameras.clear();
    defaultCamera.reset();
}

void CameraManager::Initialize()
{
	activeCameraName = nullptr;
	cameras.clear();
	// デフォルトカメラの作成
    defaultCamera = std::make_unique<Camera>();
    defaultCamera->SetTranslate({ 0, 0, -5 });
    AddCamera("default", defaultCamera.get());
    SetActiveCamera("default"); // デフォルトカメラをアクティブカメラとして設定



}

void CameraManager::AddCamera(const std::string& name, Camera* camera)
{
    if (!camera) {
        Engine::Base::Logger::Log("Warning: Attempted to add a null camera.");
        return;
    }
    const auto entry = cameras.insert_or_assign(name, camera).first;
    // 最初のカメラをアクティブに設定
    if (!activeCameraName || activeCameraName->empty()) {
        activeCameraName = &entry->first;
    }
}

void CameraManager::RemoveCamera(const std::string& name) {
    const auto entry = cameras.find(name);
    if (entry == cameras.end()) return;
    const bool wasActive = activeCameraName == &entry->first;
    if (wasActive) activeCameraName = nullptr;
    cameras.erase(entry);
    if (wasActive && !cameras.empty()) {
        activeCameraName = &cameras.begin()->first;
    }
}

Camera* CameraManager::GetCamera(const std::string& name) {
    const auto entry = cameras.find(name);
    return entry == cameras.end() ? nullptr : entry->second;
}

CameraManager::CameraIterator CameraManager::FindRegistration(std::string_view name) noexcept {
    for (auto it = cameras.begin(); it != cameras.end(); ++it)
        if (std::string_view(it->first) == name) return it;
    return cameras.end();
}

Camera* CameraManager::FindCamera(std::string_view name) noexcept {
    const auto entry = FindRegistration(name);
    return entry == cameras.end() ? nullptr : entry->second;
}

bool CameraManager::DetachCamera(std::string_view name, const Camera* expected,
    std::string_view preferredActive) noexcept {
    const auto entry = FindRegistration(name);
    if (!expected || entry == cameras.end() || entry->second != expected) return false;
    if (activeCameraName == &entry->first) {
        // Resolve views before erasing: callers may pass a view into a map key.
        auto fallback = FindRegistration(preferredActive);
        if (preferredActive.empty() || fallback == entry || fallback == cameras.end())
            fallback = FindRegistration("default");
        activeCameraName = fallback != entry && fallback != cameras.end() ? &fallback->first : nullptr;
    }
    cameras.erase(entry);
    return true;
}


Camera* CameraManager::GetActiveCamera() {
    auto activeIt = activeCameraName && !activeCameraName->empty()
        ? cameras.find(*activeCameraName) : cameras.end();
    if (activeIt == cameras.end()) {
        // アクティブカメラが無効な場合、デフォルトカメラを使用
        activeIt = FindRegistration("default");
        if (activeIt == cameras.end()) {
            activeCameraName = nullptr;
            return nullptr;
        }
        activeCameraName = &activeIt->first;
    }
    return activeIt->second;
}

void CameraManager::SetActiveCamera(const std::string& name) {
    const auto entry = cameras.find(name);
    if (entry != cameras.end()) {
        activeCameraName = &entry->first;
    } else {
        Engine::Base::Logger::Log("Warning: Attempted to set an invalid active camera. Using default camera.");
        activeCameraName = nullptr; // 無効なカメラを選択した場合、リセット
    }
}

}

