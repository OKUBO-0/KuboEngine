#pragma once
#include "BaseScene.h"

/// @brief ゲームクリア画面を表すシーンクラス
/// @details ゲームクリア時の初期化、更新、描画、終了処理を担当する。
namespace Engine::Scene {

class GameClearScene : public BaseScene
{

public:
	/// @brief ゲームクリアシーンを初期化する
	/// @param なし
	/// @return なし
	void Initialize() override;
	/// @brief ゲームクリアシーンの終了処理を行う
	/// @param なし
	/// @return なし
	void Finalize()override;
	/// @brief ゲームクリアシーンを更新する
	/// @param なし
	/// @return なし
	void Update()override;
	/// @brief ゲームクリアシーンを描画する
	/// @param なし
	/// @return なし
	void Draw()override;

private:

};

}

