#pragma once
#include "BaseScene.h"

/// @brief ゲームオーバー画面を表すシーンクラス
/// @details ゲームオーバー時の初期化、更新、描画、終了処理を担当する。
namespace Engine::Scene {

class GameOverScene : public BaseScene
{

public:
	/// @brief ゲームオーバーシーンを初期化する
	/// @param なし
	/// @return なし
	void Initialize() override;
	/// @brief ゲームオーバーシーンの終了処理を行う
	/// @param なし
	/// @return なし
	void Finalize()override;
	/// @brief ゲームオーバーシーンを更新する
	/// @param なし
	/// @return なし
	void Update()override;
	/// @brief ゲームオーバーシーンを描画する
	/// @param なし
	/// @return なし
	void Draw()override;

private:


};

}

