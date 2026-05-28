#pragma once

namespace Engine::Scene {

class SceneManager;
/// @brief すべてのシーンが満たす共通インターフェース
/// @details 初期化、終了、更新、描画と SceneManager 参照の受け取りを定義する。
class BaseScene
{
public:
	/// @brief シーンの初期化を行う
	/// @param なし
	/// @return なし
	virtual void Initialize() = 0;
	/// @brief シーンの終了処理を行う
	/// @param なし
	/// @return なし
	virtual void Finalize() = 0;
	/// @brief シーンの更新処理を行う
	/// @param なし
	/// @return なし
	virtual void Update() = 0;
	/// @brief シーンの描画処理を行う
	/// @param なし
	/// @return なし
	virtual void Draw() = 0;
	/// @brief 仮想デストラクタ
	virtual ~BaseScene() = default;

	/// @brief SceneManager 参照を受け取る
	/// @param sceneManager 管理元の SceneManager
	/// @return なし
	virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }

private:
	SceneManager* sceneManager_ = nullptr;

};

}

