#include "Framework.h"
#include "RunDiagnostics.h"
#include "RunReport.h"
#include "AbstractSceneFactory.h"
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
#include "LineCommon.h"
#include "SkyBoxCommon.h"
#include <CameraManager.h>
#include "ParticleManager.h"
#include <algorithm>
#include <cassert>
#include <atomic>
#include <cstdlib>

namespace Engine::Base {

Framework::Framework() = default;
Framework::~Framework() = default;

void Framework::Initialize()
{
	if (initializationStarted_ || finalizing_ || finalized_) {
		throw std::logic_error("Framework initialization is single-use");
	}
	initializationStarted_ = true;
	endRequest_ = false;
	frameDeltaTime_ = 1.0f / 60.0f;
	lastFrameTime_ = std::chrono::steady_clock::now();
	InitializeCoreServices();
	AcquireService(Service::Scene, Engine::Scene::SceneManager::GetInstance());
	InitializeSharedManagers();
	InitializeRenderingCommons();
	InitializeDebugTools();
}

void Framework::ConfigureWindowCloseRequestPolicy(bool deferred)
{
	if (initializationStarted_ || finalizing_ || finalized_)
		throw std::logic_error("Configure window close policy before initialization");
	if (!winApp) winApp = std::make_unique<Engine::Base::WinApp>();
	winApp->SetCloseRequestDeferred(deferred);
}

void Framework::InitializeCoreServices()
{
	if (!winApp) winApp = std::make_unique<Engine::Base::WinApp>();
	winApp->Initialize();

	dxCommon = std::make_shared<Engine::Base::DirectXCommon>();
	dxCommon->Initialize(winApp.get());

	srvManager = std::make_unique<Engine::Base::SrvManager>();
	srvManager->Initialize(dxCommon);

	offscreenRenderManager = std::make_unique<Engine::Base::OffscreenRenderManager>();
	offscreenRenderManager->Initialize(dxCommon, srvManager.get(), gameOutputExtent_);
}

void Framework::InitializeSharedManagers()
{
	AcquireService(Service::Texture, Engine::Base::TextureManager::GetInstance())->Initialize(dxCommon, srvManager.get(), GetProjectRoot());
	AcquireService(Service::Input, Engine::InputSystem::Input::GetInstance())->Initialize(winApp.get(), GetProjectRoot());
	AcquireService(Service::Audio, Engine::AudioSystem::Audio::GetInstance())->Initialize(GetProjectRoot());
	AcquireService(Service::Particle, Engine::Particle::ParticleManager::GetInstance())->Initialize(dxCommon, srvManager.get());
	AcquireService(Service::Camera, Engine::CameraSystem::CameraManager::GetInstance())->Initialize();
}

void Framework::InitializeRenderingCommons()
{
	AcquireService(Service::Sprite, Engine::Graphics2D::SpriteCommon::GetInstance())->Initialize(dxCommon);
	AcquireService(Service::Model, Engine::Graphics3D::ModelManager::GetInstance())->Initialize(dxCommon, srvManager.get());
	AcquireService(Service::Object3D, Engine::Graphics3D::Object3DCommon::GetInstance())->Initialize(dxCommon, srvManager.get());
	AcquireService(Service::Line, Engine::LineSystem::LineCommon::GetInstance())->Initialize(dxCommon, srvManager.get());
	AcquireService(Service::SkyBox, Engine::Skybox::SkyBoxCommon::GetInstance())->Initialize(dxCommon, srvManager.get());
}

void Framework::InitializeDebugTools()
{
#if !defined(KUBO_RUNTIME_ONLY) && (defined(KUBO_WITH_EDITOR) || defined(_DEBUG))
#ifndef _DEBUG
	if (!editorToolsEnabled_) return;
#endif
	imGuiManager = std::make_unique<Engine::Base::ImGuiManager>();
	imGuiManager->Initialize(dxCommon, winApp.get(), GetProjectRoot());
#endif // _DEBUG
}

void Framework::Finalize()
{
	if (finalizing_ || finalized_) return;
	if (!initializationStarted_ && !winApp && !dxCommon && !srvManager && !offscreenRenderManager && services_.PendingCount() == 0) return;
	finalizing_ = true;
	struct Guard { bool& flag; ~Guard() { flag = false; } } guard{finalizing_};
	services_.Seal();
	// Drain submitted work and discard recorded commands before any service
	// releases resources that the command list may still reference.
	if (dxCommon) dxCommon->PrepareForShutdown();
	FinalizeDebugTools();
	FinalizeSharedManagers();
	finalized_ = true;
}

void Framework::FinalizeDebugTools()
{
#if !defined(KUBO_RUNTIME_ONLY) && (defined(KUBO_WITH_EDITOR) || defined(_DEBUG))
	if (imGuiManager) imGuiManager->Finalize();
	imGuiManager.reset();
#endif // _DEBUG
}

void Framework::FinalizeSharedManagers()
{
	if (dxCommon) dxCommon->WaitForAllFrames();
	for (const auto service : { Service::Scene, Service::SkyBox, Service::Line,
		Service::Object3D, Service::Model, Service::Sprite, Service::Particle,
		Service::Camera, Service::Texture, Service::Audio, Service::Input }) {
		services_.Retire(static_cast<size_t>(service));
	}
	if (offscreenRenderManager) {
		offscreenRenderManager->Finalize();
	}
	assert((!srvManager || srvManager->GetUsedCount() == 0) &&
		"SRV descriptors remain allocated during framework shutdown");
	if (winApp) winApp->Finalize();
}

void Framework::Update()
{
	UpdateFrameDeltaTime();

	//Windowsのメッセージ処理
	if (winApp->ProcessMessage()) {
		//ゲームループを抜ける
	endRequest_ = true;
	}

	Engine::InputSystem::Input::GetInstance()->Update();
	if(auto* camera=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) camera->SetAspectRatio(gameOutputExtent_.Aspect());
	Engine::Particle::ParticleManager::GetInstance()->Update(
		Engine::Scene::SceneManager::GetInstance()->SimulationDeltaTime(frameDeltaTime_));
	Engine::Scene::SceneManager::GetInstance()->Update(frameDeltaTime_);
	Engine::LineSystem::LineCommon::GetInstance()->Update();
}

void Framework::UpdateFrameDeltaTime()
{
	const auto now = std::chrono::steady_clock::now();
	const std::chrono::duration<float> elapsed = now - lastFrameTime_;
	lastFrameTime_ = now;
	frameDeltaTime_ = std::clamp(elapsed.count(), 0.0f, 0.25f);
}

void Framework::Run()
{
	if (runActive_ || runStarted_ || initializationStarted_ || finalizing_ || finalized_)
		throw std::logic_error("Framework Run is single-use and cannot reenter an initialized host");
	// Refuse competing hosts before touching process-wide singletons. This does
	// not make direct Initialize/Finalize or other host APIs thread-safe.
	static std::atomic<Framework*> activeHost{nullptr};
	Framework* expected = nullptr;
	if (!activeHost.compare_exchange_strong(expected, this))
		throw std::logic_error("Another Framework Run already owns the shared engine services");
	runActive_ = true;
	runStarted_ = true;
	struct Admission {
		bool& running; std::atomic<Framework*>& owner;
		~Admission() { running = false; owner.store(nullptr); }
	} admission{runActive_, activeHost};
	RunReport report(GetProjectRoot());
	std::exception_ptr operation, cleanup;
	try {
		Initialize();
		while (true) { Update(); if (IsEndRequest()) break; Draw(); }
	} catch (...) { operation = std::current_exception(); }
	bool completed = false;
	unsigned attempts = 0;
	for (unsigned attempt = 0; attempt != 3; ++attempt) {
		++attempts;
		try {
			Finalize();
			if ((initializationStarted_ && !finalized_) || services_.PendingCount() != 0 ||
				(dxCommon && dxCommon->GetCommandState() != DirectXCommon::CommandState::Retired) ||
				(winApp && IsWindow(winApp->GetHwnd())))
				throw std::logic_error("Framework Finalize returned without terminal retirement");
#ifndef KUBO_RUNTIME_ONLY
			if (imGuiManager) throw std::logic_error("Framework Finalize returned with owned UI");
#endif
			completed = true;
			break;
		} catch (...) { if (!cleanup) cleanup = std::current_exception(); }
	}
	if (operation || cleanup) {
		if (!report.Save(operation, cleanup, completed, attempts)) {
			std::fputs("KuboEngine could not persist the run report in the captured project directory\n", stderr);
			OutputDebugStringA("KuboEngine run report persistence failed\n");
		}
	}
	if (!completed) {
		ReportRunException("operation error", operation);
		ReportRunException("first shutdown error", cleanup);
		std::fputs("KuboEngine shutdown incomplete after 3 attempts; exiting without host unwind\n", stderr);
		std::fflush(stderr);
		OutputDebugStringA("KuboEngine shutdown incomplete; no host unwind\n");
		std::_Exit(70);
	}
	if (operation && cleanup) throw RunFailure(operation, cleanup);
	if (cleanup) std::rethrow_exception(cleanup);
	if (operation) std::rethrow_exception(operation);
}

}
