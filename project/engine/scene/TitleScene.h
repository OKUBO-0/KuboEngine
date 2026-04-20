#pragma once
#include "BaseScene.h"

/// @brief タイトル画面を表すシーンクラス
/// @details タイトル中の初期化、更新、描画、終了処理を担当する。
namespace Engine::Scene {

class TitleScene : public BaseScene
{

public:

	/// @brief タイトルシーンを初期化する
	/// @param なし
	/// @return なし
	void Initialize()override;
	/// @brief タイトルシーンの終了処理を行う
	/// @param なし
	/// @return なし
	void Finalize()override;
	/// @brief タイトルシーンを更新する
	/// @param なし
	/// @return なし
	void Update()override;
	/// @brief タイトルシーンを描画する
	/// @param なし
	/// @return なし
	void Draw()override;

public:

	


};

}

