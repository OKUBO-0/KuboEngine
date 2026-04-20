#pragma once
#include "IParticleBehavior.h"

/// @brief 魔法陣演出用パーティクルの振る舞いを定義するクラス
/// @details 魔法陣エフェクト向けの生成位置、拡縮、色変化などを実装する。
namespace Engine::Particle {

class MagicCircleBehavior : public IParticleBehavior
{
public:
	/// @brief 魔法陣パーティクルの初期状態を生成する
	/// @param rng 乱数生成器
	/// @param pos 発生位置
	/// @return 初期化済みパーティクル
	Particle Create(std::mt19937& rng, const Vector3& pos) override;

	/// @brief 魔法陣パーティクルを 1 フレーム更新する
	/// @param particle 更新対象パーティクル
	/// @param dt 経過時間
	/// @param materialData 描画用マテリアル
	/// @return なし
	void Update(Particle& particle, float dt, ::Material* materialData) override;

};

}

