#include "Line.h"
#include "LineCommon.h"
#include <cassert>
#include <numbers>
#include <utility>
#include "Vector3.h"

namespace {

const Vector4 kDefaultGridLineColor = { 0.5f, 0.5f, 0.5f, 1.0f };

}

namespace Engine::LineSystem {

std::array<Vector3, 8> Line::CreateAabbVertices(const Vector3& min, const Vector3& max) const
{
	return {
		Vector3{ min.x, min.y, min.z },
		Vector3{ max.x, min.y, min.z },
		Vector3{ max.x, max.y, min.z },
		Vector3{ min.x, max.y, min.z },
		Vector3{ min.x, min.y, max.z },
		Vector3{ max.x, min.y, max.z },
		Vector3{ max.x, max.y, max.z },
		Vector3{ min.x, max.y, max.z }
	};
}

std::array<Vector3, 8> Line::CreateObbVertices(const Engine::Math::OBB& obb) const
{
	const Vector3 axisX = obb.orientations[0] * obb.size.x;
	const Vector3 axisY = obb.orientations[1] * obb.size.y;
	const Vector3 axisZ = obb.orientations[2] * obb.size.z;
	return {
		obb.center - axisX - axisY - axisZ,
		obb.center + axisX - axisY - axisZ,
		obb.center + axisX + axisY - axisZ,
		obb.center - axisX + axisY - axisZ,
		obb.center - axisX - axisY + axisZ,
		obb.center + axisX - axisY + axisZ,
		obb.center + axisX + axisY + axisZ,
		obb.center - axisX + axisY + axisZ,
	};
}

void Line::DrawAabbEdges(const std::array<Vector3, 8>& vertices, const Vector4& color)
{
	static constexpr std::array<std::pair<uint32_t, uint32_t>, 12> kAabbEdges = {{
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7},
	}};

	LineCommon* lineCommon = LineCommon::GetInstance();
	for (const auto& [startIndex, endIndex] : kAabbEdges) {
		lineCommon->DrawLine(vertices[startIndex], vertices[endIndex], color);
	}
}

void Line::Draw(const Vector3& start, const Vector3& end, const Vector4& color)
{
	LineCommon::GetInstance()->DrawLine(start, end, color);
}

void Line::DrawAABB(const Vector3& min, const Vector3& max, const Vector4& color)
{
	const std::array<Vector3, 8> vertices = CreateAabbVertices(min, max);
	DrawAabbEdges(vertices, color);
}

void Line::DrawOBB(const Engine::Math::OBB& obb, const Vector4& color)
{
	const std::array<Vector3, 8> vertices = CreateObbVertices(obb);
	DrawAabbEdges(vertices, color);
}

void Line::DrawAABBVector3(Vector3 center, float radius, Vector4 color)
{
	const Vector3 min = center - radius;
	const Vector3 max = center + radius;
	DrawAABB(min, max, color);
}

void Line::DrawGridLinesAlongZ(const Vector3& center, float start, float end, float step, uint32_t subdivision)
{
	LineCommon* lineCommon = LineCommon::GetInstance();
	for (uint32_t i = 0; i <= subdivision; ++i) {
		const float z = start + step * i;
		lineCommon->DrawLine(
			{ center.x + start, center.y, center.z + z },
			{ center.x + end, center.y, center.z + z },
			kDefaultGridLineColor);
	}
}

void Line::DrawGridLinesAlongX(const Vector3& center, float start, float end, float step, uint32_t subdivision)
{
	LineCommon* lineCommon = LineCommon::GetInstance();
	for (uint32_t i = 0; i <= subdivision; ++i) {
		const float x = start + step * i;
		lineCommon->DrawLine(
			{ center.x + x, center.y, center.z + start },
			{ center.x + x, center.y, center.z + end },
			kDefaultGridLineColor);
	}
}

void Line::DrawGrid(Vector3 center, float Gridhalfwidth, uint32_t Subdivision)
{
	assert(Subdivision > 0);

	const float step = (Gridhalfwidth * 2.0f) / Subdivision;
	const float start = -Gridhalfwidth;
	const float end = Gridhalfwidth;
	DrawGridLinesAlongZ(center, start, end, step, Subdivision);
	DrawGridLinesAlongX(center, start, end, step, Subdivision);
}

Vector3 Line::CalculateSpherePoint(const Vector3& center, float radius, float latitude, float longitude) const
{
	return {
		radius * std::cosf(latitude) * std::cosf(longitude) + center.x,
		radius * std::sinf(latitude) + center.y,
		radius * std::cosf(latitude) * std::sinf(longitude) + center.z
	};
}

void Line::DrawSphere(const Vector3& center, float radius, const Vector4& color)
{
	const uint32_t kSbdivision = 16;
	const float kLonEvery = 2 * std::numbers::pi_v<float> / kSbdivision;
	const float kLatEvery = std::numbers::pi_v<float> / kSbdivision;

	for (uint32_t latIndex = 0; latIndex < kSbdivision; ++latIndex) {
		const float latitude = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;
		for (uint32_t lonIndex = 0; lonIndex < kSbdivision; ++lonIndex) {
			const float longitude = lonIndex * kLonEvery;
			const Vector3 current = CalculateSpherePoint(center, radius, latitude, longitude);
			const Vector3 nextLatitude = CalculateSpherePoint(center, radius, latitude + kLatEvery, longitude);
			const Vector3 nextLongitude = CalculateSpherePoint(center, radius, latitude, longitude + kLonEvery);
			LineCommon::GetInstance()->DrawLine(current, nextLatitude, color);
			LineCommon::GetInstance()->DrawLine(current, nextLongitude, color);
		}
	}
}

void Line::DrawJointToParent(const Joint& joint, const Vector3& jointPosition,
	const std::vector<Matrix4x4>& skeletonPose, const Matrix4x4& worldMatrix, const Vector4& color)
{
	if (!joint.parent.has_value()) {
		return;
	}

	const int parentIndex = joint.parent.value();
	const Vector3 parentPosition = MyMath::Transform(MyMath::GetTranslate(skeletonPose[parentIndex]), worldMatrix);
	LineCommon::GetInstance()->DrawLine(parentPosition, jointPosition, color);
}

void Line::DrawSkeleton(const Skeleton& skeleton, const std::vector<Matrix4x4>& skeletonPose, const Matrix4x4& worldMatrix, const Vector4& color)
{
	for (const Joint& joint : skeleton.joints) {
		const Vector3 jointPosition = MyMath::Transform(MyMath::GetTranslate(skeletonPose[joint.index]), worldMatrix);
		DrawSphere(jointPosition, 0.005f, color);
		DrawJointToParent(joint, jointPosition, skeletonPose, worldMatrix, color);
	}
}

}
