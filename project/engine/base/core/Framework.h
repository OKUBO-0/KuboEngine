#pragma once
#include <chrono>
#include <memory>

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
	/// @param なし
	/// @return なし
	void Run();

	/// @brief 終了要求の有無を返す
	/// @param なし
	/// @return 終了要求があれば true
	virtual bool IsEndRequest() const { return endRequest_; }

	/// @brief 直近フレームの経過秒を返す
	/// @return 経過秒
	float GetFrameDeltaTime() const { return frameDeltaTime_; }

protected:
	void InitializeCoreServices();
	void InitializeSharedManagers();
	void InitializeRenderingCommons();
	void InitializeDebugTools();
	void FinalizeDebugTools();
	void FinalizeSharedManagers();
	void UpdateFrameDeltaTime();

	// ゲーム終了フラグ
	bool endRequest_ = false;
	float frameDeltaTime_ = 1.0f / 60.0f;
	std::chrono::steady_clock::time_point lastFrameTime_;

	// WinAppのポインタ
	std::unique_ptr<Engine::Base::WinApp> winApp;
	// DirectXCommonのポインタ
	std::unique_ptr<Engine::Base::DirectXCommon> dxCommon;
	// SrvManagerのポインタ
	std::unique_ptr<Engine::Base::SrvManager> srvManager;
	// ImGuiManagerのポインタ
	std::unique_ptr<Engine::Base::ImGuiManager> imGuiManager;
	// SceneFactoryのポインタ
	std::unique_ptr<Engine::Scene::AbstractSceneFactory> sceneFactory;
	std::unique_ptr<Engine::Base::OffscreenRenderManager> offscreenRenderManager;
};

}
