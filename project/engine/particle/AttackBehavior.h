#pragma once
#include "IParticleBehavior.h"

/// @brief 攻撃演出用パーティクルの振る舞いを定義するクラス
/// @details 攻撃エフェクト向けの生成位置、速度、色変化などを実装する。
namespace Engine::Particle {

class AttackBehavior : public IParticleBehavior
{
public:
	/// @brief 攻撃パーティクルの初期状態を生成する
	/// @param rng 乱数生成器
	/// @param pos 発生位置
	/// @return 初期化済みパーティクル
	Particle Create(std::mt19937& rng, const Vector3& pos) override;

	/// @brief 攻撃パーティクルを 1 フレーム更新する
	/// @param particle 更新対象パーティクル
	/// @param dt 経過時間
	/// @param materialData 描画用マテリアル
	/// @return なし
	void Update(Particle& particle, float dt, ::Material* materialData) override;

};

}

