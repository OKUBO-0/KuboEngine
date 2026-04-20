#pragma once

#include <memory>

namespace Engine::InputSystem {
class Input;
}

namespace Engine::Scene {

class GamePlayScene;

/// @brief プレイヤー操作状態の共通インターフェース
/// @details GamePlayScene の入力処理を状態オブジェクトへ委譲し、
///          移動、拡縮、回転の振る舞いを状態単位で切り替える。
class PlayerControlState {
public:
	/// @brief 仮想デストラクタ
	/// @param なし
	/// @return なし
	virtual ~PlayerControlState() = default;

	/// @brief 現在状態の入力処理を実行する
	/// @param scene 操作対象のゲームプレイシーン
	/// @param input 入力管理クラス
	/// @return 次状態。遷移しない場合は nullptr
	virtual std::unique_ptr<PlayerControlState> Update(GamePlayScene& scene, Engine::InputSystem::Input& input) = 0;

	/// @brief 状態名を取得する
	/// @param なし
	/// @return 状態名
	virtual const char* GetName() const = 0;
};

/// @brief 待機状態
/// @details 入力がない通常状態で、他状態への遷移判定のみを担当する。
class IdlePlayerControlState final : public PlayerControlState {
public:
	std::unique_ptr<PlayerControlState> Update(GamePlayScene& scene, Engine::InputSystem::Input& input) override;
	const char* GetName() const override;
};

/// @brief 移動状態
/// @details 左スティック入力に応じてプレイヤー位置を更新する。
class MovePlayerControlState final : public PlayerControlState {
public:
	std::unique_ptr<PlayerControlState> Update(GamePlayScene& scene, Engine::InputSystem::Input& input) override;
	const char* GetName() const override;
};

/// @brief 拡縮状態
/// @details トリガー入力に応じてプレイヤーのスケールを更新する。
class ScalePlayerControlState final : public PlayerControlState {
public:
	std::unique_ptr<PlayerControlState> Update(GamePlayScene& scene, Engine::InputSystem::Input& input) override;
	const char* GetName() const override;
};

/// @brief 回転状態
/// @details ショルダーボタン入力に応じてプレイヤー回転を更新する。
class RotatePlayerControlState final : public PlayerControlState {
public:
	std::unique_ptr<PlayerControlState> Update(GamePlayScene& scene, Engine::InputSystem::Input& input) override;
	const char* GetName() const override;
};

}
