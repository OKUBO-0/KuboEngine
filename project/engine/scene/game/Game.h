#include "Framework.h"
#include <memory>
#include <string>

namespace Engine::Scene {
class AbstractSceneFactory;
}

/// @brief アプリケーション固有のシーン制御と描画順を定義するクラス
/// @details Framework を継承し、シーン生成とオフスクリーン描画の流れを構築する。
namespace Engine::Scene {

class Game : public Engine::Base::Framework {
public:
	Game();
	explicit Game(std::unique_ptr<AbstractSceneFactory> sceneFactory, std::string initialSceneName = "GAMEPLAY");

	void SetSceneFactory(std::unique_ptr<AbstractSceneFactory> sceneFactory);
	void SetInitialSceneName(std::string sceneName);

	/// @brief ゲーム固有の初期化を行う
	/// @param なし
	/// @return なし
	void Initialize() override;
	/// @brief ゲーム固有の終了処理を行う
	/// @param なし
	/// @return なし
	void Finalize() override;
	/// @brief ゲーム全体の更新処理を行う
	/// @param なし
	/// @return なし
	void Update() override;
	/// @brief シーン描画と最終合成を行う
	/// @param なし
	/// @return なし
	void Draw() override;

private:
	void DrawDebugEditorShell();

	std::string initialSceneName_;
	bool debugSceneViewOpen_ = true;
	bool debugStatsOpen_ = true;
	bool debugSceneSettingsOpen_ = true;
	bool debugWindowSwitcherOpen_ = false;
};

}
