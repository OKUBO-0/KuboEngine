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
    cameras.clear();
    defaultCamera.reset();
    activeCameraName.clear();
}

void CameraManager::Initialize()
{
	cameras.clear();
	activeCameraName.clear();
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
    cameras[name] = camera;
    // 最初のカメラをアクティブに設定
    if (activeCameraName.empty()) {
        activeCameraName = name;
    }
}

void CameraManager::RemoveCamera(const std::string& name) {
    if (cameras.erase(name) > 0 && activeCameraName == name) {
        // アクティブカメラが削除された場合、他のカメラをアクティブに設定
        if (!cameras.empty()) {
            activeCameraName = cameras.begin()->first;
        }
        else {
            activeCameraName.clear();
        }
    }
}

Camera* CameraManager::GetCamera(const std::string& name) {
    auto it = cameras.find(name);
    if (it != cameras.end()) {
        return it->second;
    }
    return nullptr;
}


Camera* CameraManager::GetActiveCamera() {
    auto activeIt = cameras.find(activeCameraName);
    if (activeCameraName.empty() || activeIt == cameras.end()) {
        // アクティブカメラが無効な場合、デフォルトカメラを使用
        activeIt = cameras.find("default");
        if (activeIt == cameras.end()) {
            activeCameraName.clear();
            return nullptr;
        }
        activeCameraName = "default";
    }
    return activeIt->second;
}

void CameraManager::SetActiveCamera(const std::string& name) {
    if (cameras.find(name) != cameras.end()) {
        activeCameraName = name;
    } else {
        Engine::Base::Logger::Log("Warning: Attempted to set an invalid active camera. Using default camera.");
        activeCameraName.clear(); // 無効なカメラを選択した場合、リセット
    }
}

}

