#pragma once

/// @brief 4 次元ベクトル
/// @details RGBA カラーや同次座標を表現するための値オブジェクト。
namespace Engine::Math {

struct Vector4 final {
	float x;
	float y;
	float z;
	float w;
};

}

using Engine::Math::Vector4;
