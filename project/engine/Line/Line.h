#pragma once

#include <array>
#include "MyMath.h"
#include "RenderingData.h"

/// @brief デバッグ用ラインプリミティブを発行するユーティリティクラス
/// @details 単線、AABB、グリッド、球、スケルトンなどをライン群へ変換し、
///          LineCommon に描画要求を送る役割を持つ。
namespace Engine::LineSystem {

class LineCommon;
class Line
{
public:
	/// @brief 2点間のラインを描画する
	/// @param start 開始点
	/// @param end 終了点
	/// @param color ライン色
	/// @return なし
	void Draw(const Vector3& start, const Vector3& end, const Vector4& color);

	/// @brief AABB をラインで描画する
	/// @param min 最小座標
	/// @param max 最大座標
	/// @param color ライン色
	/// @return なし
	void DrawAABB(const Vector3& min, const Vector3& max, const Vector4& color);
	void DrawOBB(const Engine::Math::OBB& obb, const Vector4& color);

	/// @brief 中心と半径から AABB を描画する
	/// @param center 中心座標
	/// @param radius 各軸方向の半径
	/// @param color ライン色
	/// @return なし
	void DrawAABBVector3( Vector3 center, float radius, Vector4 color);

	/// @brief 指定中心を基準にグリッドを描画する
	/// @param center グリッド中心
	/// @param Gridhalfwidth グリッドの半幅
	/// @param Subdivision 分割数
	/// @return なし
	void DrawGrid(Vector3 center,float Gridhalfwidth = 2.0, uint32_t Subdivision = 50);

	/// @brief 球のワイヤーフレームを描画する
	/// @param center 球の中心
	/// @param radius 球の半径
	/// @param color ライン色
	/// @return なし
	void DrawSphere(const Vector3& center, float radius, const Vector4& color);

	/// @brief スケルトンの接続関係をラインで描画する
	/// @param skeleton スケルトン情報
	/// @param skeletonPose ジョイント行列群
	/// @param worldMatrix ワールド行列
	/// @param color ライン色
	/// @return なし
	void DrawSkeleton(const Skeleton& skeleton, const std::vector<Matrix4x4>& skeletonPose, const Matrix4x4& worldMatrix, const Vector4& color={1.0f,1.0f,1.0f,1.0f});

private:
	std::array<Vector3, 8> CreateAabbVertices(const Vector3& min, const Vector3& max) const;
	std::array<Vector3, 8> CreateObbVertices(const Engine::Math::OBB& obb) const;
	void DrawAabbEdges(const std::array<Vector3, 8>& vertices, const Vector4& color);
	void DrawGridLinesAlongZ(const Vector3& center, float start, float end, float step, uint32_t subdivision);
	void DrawGridLinesAlongX(const Vector3& center, float start, float end, float step, uint32_t subdivision);
	Vector3 CalculateSpherePoint(const Vector3& center, float radius, float latitude, float longitude) const;
	void DrawJointToParent(const Joint& joint, const Vector3& jointPosition,
		const std::vector<Matrix4x4>& skeletonPose, const Matrix4x4& worldMatrix, const Vector4& color);

};

}
