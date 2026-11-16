#pragma once
#include <chrono>
#include <memory>
#include "../ProjectPaths.h"
#include "../render/ViewExtent.h"
#include "ServiceRetirementJournal.h"
#include "RunFailure.h"
#include <stdexcept>

namespace Engine::Scene {
class AbstractSceneFactory;
}

/// @brief アプリケーション全体の実行基盤を管理する抽象クラス
/// @details ウィンドウ、DirectX、入力、描画共通機能を初期化し、
///          派生クラスに更新・描画処理を委譲する。
namespace Engine::Base {

class DirectXCommon;
class ImGuiManager;
class OffscreenRenderManager;
class SrvManager;
class WinApp;

class Framework {
public:
	Framework();
	virtual ~Framework();
	/// @brief フレームワーク共通の初期化を行う
	/// @param なし
	/// @return なし
	virtual void Initialize();
	/// @brief フレームワーク共通の終了処理を行う
	/// @param なし
	/// @return なし
	virtual void Finalize();
	/// @brief フレームワーク共通の更新処理を行う
	/// @param なし
	/// @return なし
	virtual void Update();
	/// @brief 派生クラス固有の描画処理を行う
	/// @param なし
	/// @return なし
	virtual void Draw() = 0;

	/// @brief 初期化から終了までのメインループを実行する
	// Single use; cannot overlap another Run (shared services are process-wide).
	// Errors propagate only after terminal cleanup. Persistent cleanup failure
	// reports both errors and exits without destructing a still-active host.
	/// @param なし
	/// @return なし
	void Run();

	// Configure before Initialize/Run; legacy callers snapshot their construction cwd.
	void SetProjectRoot(const std::filesystem::path& root) { projectPaths_ = ProjectPaths(root); }
	const std::filesystem::path& GetProjectRoot() const { return projectPaths_.Root(); }
	void SetGameOutputExtent(ViewExtent extent) {
		if(dxCommon) throw std::logic_error("Configure Game output before initializing the application");
		if(!ValidGameOutputExtent(extent)) throw std::invalid_argument("Invalid Game output extent");
		gameOutputExtent_=extent;
	}
	ViewExtent GetGameOutputExtent() const { return gameOutputExtent_; }

	/// @brief 終了要求の有無を返す
	/// @param なし
	/// @return 終了要求があれば true
	virtual bool IsEndRequest() const { return endRequest_; }

	/// @brief 直近フレームの経過秒を返す
	/// @return 経過秒
	float GetFrameDeltaTime() const { return frameDeltaTime_; }

protected:
	ProjectPaths projectPaths_;
	ViewExtent gameOutputExtent_{};
	// Configure the owned WinApp before native window creation can dispatch close.
	void ConfigureWindowCloseRequestPolicy(bool deferred);
	void InitializeCoreServices();
	void InitializeSharedManagers();
	void InitializeRenderingCommons();
	void InitializeDebugTools();
	void FinalizeDebugTools();
	void FinalizeSharedManagers();
	void UpdateFrameDeltaTime();

	// ゲーム終了フラグ
	bool endRequest_ = false;
	bool editorToolsEnabled_ = true;
	float frameDeltaTime_ = 1.0f / 60.0f;
	std::chrono::steady_clock::time_point lastFrameTime_;

	// WinAppのポインタ
	std::unique_ptr<Engine::Base::WinApp> winApp;
	// Framework が所有し、各描画共通クラスは weak/shared で参照する。
	std::shared_ptr<Engine::Base::DirectXCommon> dxCommon;
	// SrvManagerのポインタ
	std::unique_ptr<Engine::Base::SrvManager> srvManager;
	// ImGuiManagerのポインタ
	#ifndef KUBO_RUNTIME_ONLY
	std::unique_ptr<Engine::Base::ImGuiManager> imGuiManager;
	#endif
	// SceneFactoryのポインタ
	std::unique_ptr<Engine::Scene::AbstractSceneFactory> sceneFactory;
	std::unique_ptr<Engine::Base::OffscreenRenderManager> offscreenRenderManager;

	// Record immediately after GetInstance returns, before Initialize can throw.
	enum class Service : size_t { Scene, Texture, Input, Audio, Particle, Camera,
		Sprite, Model, Object3D, Line, SkyBox, Count };
	ServiceRetirementJournal<static_cast<size_t>(Service::Count)> services_;
	bool initializationStarted_ = false;
	bool finalizing_ = false;
	bool finalized_ = false;
	bool runActive_ = false;
	bool runStarted_ = false;
	template <class Manager> Manager* AcquireService(Service service, Manager* manager) {
		services_.Record(static_cast<size_t>(service), manager, [](void* instance) {
			static_cast<Manager*>(instance)->Finalize();
		});
		return manager;
	}
};

}
