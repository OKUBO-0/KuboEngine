#include "Game.h"
#include "Framework.h"
#include "D3DResourceLeakChecker.h"
#include "GameSceneFactory.h"
#include "SceneId.h"
#include <Windows.h>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

namespace {

	/// @brief アプリケーション実行の起動処理をまとめるヘルパークラス
	/// @details WinMain は Windows の都合で残し、実際の初期化と実行責務はこのクラスへ委譲する。
	class Application final {
	public:
		/// @brief ゲーム本体を初期化して実行する
		/// @param なし
		/// @return 終了コード
		int Run() const {
			// DirectX リソースリーク検出用オブジェクト
			Engine::Base::D3DResourceLeakChecker leakCheck;

			// デバッグビルド時に起動確認メッセージを出しておく
			OutputDebugStringA("Hello, DirectX!\n");

			// Framework を継承した Game を生成して、実行責務を一本化する
			auto directXGame = std::make_unique<Engine::Scene::Game>(
				std::make_unique<DirectXGame::GameSceneFactory>(),
				DirectXGame::SceneId::kTitle);
			std::unique_ptr<Engine::Base::Framework> game = std::move(directXGame);
			game->Run();

			return 0;
		}
	};

}

/// @brief Windows サブシステムが要求するプロセスエントリーポイント
/// @details 外部公開される自由関数はこの OS エントリーポイントだけに留め、実行責務は Application へ委譲する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	try {
		return Application{}.Run();
	} catch (const std::exception& error) {
		const std::string message =
			std::string("KuboEngine failed:\n") + error.what();
		OutputDebugStringA((message + '\n').c_str());
		MessageBoxA(
			nullptr,
			message.c_str(),
			"KuboEngine Error",
			MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	} catch (...) {
		constexpr char message[] =
			"KuboEngine failed with an unknown exception.";
		OutputDebugStringA(message);
		MessageBoxA(
			nullptr,
			message,
			"KuboEngine Error",
			MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}
}
