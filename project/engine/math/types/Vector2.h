#pragma once

/// @brief 2 次元ベクトル
/// @details 主に画面座標や UV 座標の保持に使う単純な値オブジェクト。
namespace Engine::Math {

struct Vector2 final {
	float x;
	float y;
	
};

}

using Engine::Math::Vector2;
