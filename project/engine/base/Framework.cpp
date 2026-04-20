#include "Framework.h"
#include "Input.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SpriteCommon.h"
#include "Object3DCommon.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "ImGuiManager.h"
#include "Audio.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "OffscreenRenderManager.h"
#include "Linecommon.h"
#include "SkyBoxCommon.h"
#include <CameraManager.h>
#include "ParticleManager.h"

namespace Engine::Base {

Framework::~Framework() = default;

void Framework::Initialize()
{
	endRequest_ = false;
	InitializeCoreServices();
	InitializeSharedManagers();
	InitializeRenderingCommons();
	InitializeDebugTools();
}

void Framework::InitializeCoreServices()
{
	winApp = std::make_unique<Engine::Base::WinApp>();
	winApp->Initialize();

	dxCommon = std::make_unique<Engine::Base::DirectXCommon>();
	dxCommon->Initialize(winApp.get());

	srvManager = std::make_unique<Engine::Base::SrvManager>();
	srvManager->Initialize(dxCommon.get());

	offscreenRenderManager = std::make_unique<Engine::Base::OffscreenRenderManager>();
	offscreenRenderManager->Initialize(dxCommon.get(), srvManager.get());
}

void Framework::InitializeSharedManagers()
{
	Engine::Base::TextureManager::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
	Engine::InputSystem::Input::GetInstance()->Initialize(winApp.get());
	Engine::AudioSystem::Audio::GetInstance()->Initialize();
	Engine::Particle::ParticleManager::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
}

void Framework::InitializeRenderingCommons()
{
	Engine::Graphics2D::SpriteCommon::GetInstance()->Initialize(dxCommon.get());
	Engine::Graphics3D::ModelManager::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
	Engine::Graphics3D::Object3DCommon::GetInstance()->Initialize(dxCommon.get(),srvManager.get());
	LineCommon::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
	Engine::Skybox::SkyBoxCommon::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
}

void Framework::InitializeDebugTools()
{
#ifdef _DEBUG
	imGuiManager = std::make_unique<Engine::Base::ImGuiManager>();
	imGuiManager->Initialize(dxCommon.get(), winApp.get());
#endif // _DEBUG
}

void Framework::Finalize()
{
	FinalizeDebugTools();
	FinalizeSharedManagers();
}

void Framework::FinalizeDebugTools()
{
#ifdef _DEBUG
	imGuiManager->Finalize();
	imGuiManager.reset();
#endif // _DEBUG
}

void Framework::FinalizeSharedManagers()
{
	Engine::AudioSystem::Audio::GetInstance()->Finalize();
	winApp->Finalize();
	Engine::Base::TextureManager::GetInstance()->Finalize();
	Engine::Graphics3D::ModelManager::GetInstance()->Finalize();
	Engine::CameraSystem::CameraManager::GetInstance()->Finalize();
	Engine::Particle::ParticleManager::GetInstance()->Finalize();
	Engine::Skybox::SkyBoxCommon::GetInstance()->Finalize();
	Engine::InputSystem::Input::GetInstance()->Finalize();
	Engine::Graphics2D::SpriteCommon::GetInstance()->Finalize();
	Engine::Graphics3D::Object3DCommon::GetInstance()->Finalize();
	Engine::Scene::SceneManager::GetInstance()->Finalize();
	LineCommon::GetInstance()->Finalize();
}

void Framework::Update()
{
	//Windowsのメッセージ処理
	if (winApp->ProcessMessage()) {
		//ゲームループを抜ける
	endRequest_ = true;
	}

	Engine::InputSystem::Input::GetInstance()->Update();
	Engine::Particle::ParticleManager::GetInstance()->Update();
	Engine::Scene::SceneManager::GetInstance()->Update();
	LineCommon::GetInstance()->Update();
}

void Framework::Run()
{
	Initialize();
	while (true) {
		Update();
		if (IsEndRequest()) {
			break;
		}
		Draw();
	}
	Finalize();
}

}
