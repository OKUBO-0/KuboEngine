#pragma once


#include <random>        
#include "Vector3.h"

namespace Engine::Math {
struct Material;
}

namespace Engine::Particle {

struct Particle;

/// @brief パーティクル生成と更新ロジックを差し替えるためのインターフェース
/// @details ParticleManager から呼ばれ、生成時の初期値設定と毎フレーム更新を
///          振る舞いごとに切り替えるための Strategy を表す。
class IParticleBehavior
{
public:
	virtual~IParticleBehavior() = default;

	/// @brief 既存パーティクルを 1 フレーム更新する
	/// @param particle 更新対象パーティクル
	/// @param dt 経過時間
	/// @param materialData 描画用マテリアル
	/// @return なし
	virtual void Update(Particle& particle, float dt, Engine::Math::Material* materialData) = 0;

	/// @brief 新規パーティクルの初期状態を生成する
	/// @param rng 乱数生成器
	/// @param pos 発生位置
	/// @return 初期化済みパーティクル
	virtual Particle Create(std::mt19937& rng, const Vector3& pos) = 0;

};

}

