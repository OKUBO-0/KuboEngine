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

    // デフォルトカメラの作成
    defaultCamera = std::make_unique<Camera>();
    defaultCamera->SetTranslate({ 0, 0, -5 });
    AddCamera("default", defaultCamera.get());
    SetActiveCamera("default"); // デフォルトカメラをアクティブカメラとして設定



}

void CameraManager::AddCamera(const std::string& name, const Camera* camera)
{
    cameras[name] = *camera; // Dereference the pointer to store the Camera object
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
        return &(it->second);
    }
    return nullptr;
}


Camera* CameraManager::GetActiveCamera() {
    if (activeCameraName.empty() || cameras.find(activeCameraName) == cameras.end()) {
        // アクティブカメラが無効な場合、デフォルトカメラを使用
        SetActiveCamera("default"); // デフォルトカメラをアクティブカメラとして設定
        return defaultCamera.get();
    }
    return &cameras[activeCameraName];
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

