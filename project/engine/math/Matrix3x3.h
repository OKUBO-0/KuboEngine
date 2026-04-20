#pragma once
/// @brief 3x3 行列
/// @details 回転や法線変換などの小規模な行列演算に用いる基本データ構造。
struct Matrix3x3 final {
	float m[3][3];
};
