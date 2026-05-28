#define NOMINMAX 
#include "MyMath.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <numbers>

namespace {
Matrix4x4 MakeMatrixFromRows(
	const Vector4& row0,
	const Vector4& row1,
	const Vector4& row2,
	const Vector4& row3)
{
	return Matrix4x4{
		row0.x, row0.y, row0.z, row0.w,
		row1.x, row1.y, row1.z, row1.w,
		row2.x, row2.y, row2.z, row2.w,
		row3.x, row3.y, row3.z, row3.w
	};
}

Vector3 SelectOrthogonalAxis(const Vector3& normalizedVector)
{
	return (std::abs(normalizedVector.x) < 0.1f) ? Vector3{ 1, 0, 0 } : Vector3{ 0, 1, 0 };
}

Vector3 ClampPointToAabb(const Vector3& point, const AABB& aabb)
{
	return {
		std::clamp(point.x, aabb.min.x, aabb.max.x),
		std::clamp(point.y, aabb.min.y, aabb.max.y),
		std::clamp(point.z, aabb.min.z, aabb.max.z)
	};
}

bool IsPointInsideTriangleOnPlane(
	const Vector3& point,
	const Triangle& triangle,
	const Plane& plane,
	const Vector3& edge01,
	const Vector3& edge12,
	const Vector3& edge20)
{
	Vector3 pointFromV1 = point - triangle.vertices[1];
	Vector3 pointFromV2 = point - triangle.vertices[2];
	Vector3 pointFromV0 = point - triangle.vertices[0];

	Vector3 cross01 = edge01.Cross(pointFromV1);
	Vector3 cross12 = edge12.Cross(pointFromV2);
	Vector3 cross20 = edge20.Cross(pointFromV0);
	return MyMath::Dot(cross01, plane.normal) >= 0.0f &&
		MyMath::Dot(cross12, plane.normal) >= 0.0f &&
		MyMath::Dot(cross20, plane.normal) >= 0.0f;
}

void CalculateAabbSegmentParamRange(const AABB& aabb, const Segment& segment, Vector3& tNear, Vector3& tFar)
{
	Vector3 tMin{
		(aabb.min.x - segment.origin.x) / segment.diff.x,
		(aabb.min.y - segment.origin.y) / segment.diff.y,
		(aabb.min.z - segment.origin.z) / segment.diff.z,
	};
	Vector3 tMax{
		(aabb.max.x - segment.origin.x) / segment.diff.x,
		(aabb.max.y - segment.origin.y) / segment.diff.y,
		(aabb.max.z - segment.origin.z) / segment.diff.z,
	};

	tNear = {
		std::min(tMin.x, tMax.x),
		std::min(tMin.y, tMax.y),
		std::min(tMin.z, tMax.z)
	};
	tFar = {
		std::max(tMin.x, tMax.x),
		std::max(tMin.y, tMax.y),
		std::max(tMin.z, tMax.z)
	};
}
}



namespace Engine::Math {

Matrix4x4 MyMath::MakeTranslateMatrix(const Vector3& translate)

{
	Matrix4x4 ans;

	ans.m[0][0] = 1;
	ans.m[0][1] = 0;
	ans.m[0][2] = 0;
	ans.m[0][3] = 0;

	ans.m[1][0] = 0;
	ans.m[1][1] = 1;
	ans.m[1][2] = 0;
	ans.m[1][3] = 0;

	ans.m[2][0] = 0;
	ans.m[2][1] = 0;
	ans.m[2][2] = 1;
	ans.m[2][3] = 0;

	ans.m[3][0] = translate.x;
	ans.m[3][1] = translate.y;
	ans.m[3][2] = translate.z;
	ans.m[3][3] = 1;

	return ans;

}


Matrix4x4 MyMath::MakeScaleMatrix(const Vector3& scale)

{
	Matrix4x4 ans;

	ans.m[0][0] = scale.x;
	ans.m[0][1] = 0;
	ans.m[0][2] = 0;
	ans.m[0][3] = 0;

	ans.m[1][0] = 0;
	ans.m[1][1] = scale.y;
	ans.m[1][2] = 0;
	ans.m[1][3] = 0;

	ans.m[2][0] = 0;
	ans.m[2][1] = 0;
	ans.m[2][2] = scale.z;
	ans.m[2][3] = 0;

	ans.m[3][0] = 0;
	ans.m[3][1] = 0;
	ans.m[3][2] = 0;
	ans.m[3][3] = 1;
	return ans;

}


Vector3 MyMath::Transform(const Vector3& vector, const Matrix4x4& matrix)

{

	Vector3 ans;

	ans.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
	ans.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
	ans.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];
	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];

	assert(w != 0.0f);
	ans.x /= w;
	ans.y /= w;
	ans.z /= w;
	return ans;

}

Vector3 MyMath::Normalize(const Vector3& vector)
{

	float length = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
	return Vector3(vector.x / length, vector.y / length, vector.z / length);
}

//Add
Vector3 MyMath::Add(const Vector3& v, float scalar) {
	return Vector3(v.x + scalar, v.y + scalar, v.z + scalar);
}
Vector3 MyMath::Add(const Vector3& v1, const Vector3& v2) {
	return { v1.x + v2.x,v1.y + v2.y,v1.z + v2.z };
}

Vector3 MyMath::Cross(const Vector3& v1, const Vector3& v2)
{
	Vector3 ans;
	ans.x = v1.y * v2.z - v1.z * v2.y;
	ans.y = v1.z * v2.x - v1.x * v2.z;
	ans.z = v1.x * v2.y - v1.y * v2.x;
	return ans;


}

Matrix4x4 MyMath::MakeRotateXMatrix(float radian)

{

	Matrix4x4 ans;
	ans.m[0][0] = 1;
	ans.m[0][1] = 0;
	ans.m[0][2] = 0;
	ans.m[0][3] = 0;

	ans.m[1][0] = 0;
	ans.m[1][1] = std::cos(radian);;
	ans.m[1][2] = std::sin(radian);;
	ans.m[1][3] = 0;

	ans.m[2][0] = 0;
	ans.m[2][1] = -std::sin(radian);;
	ans.m[2][2] = std::cos(radian);;
	ans.m[2][3] = 0;

	ans.m[3][0] = 0;
	ans.m[3][1] = 0;
	ans.m[3][2] = 0;
	ans.m[3][3] = 1;

	return ans;

}


Matrix4x4 MyMath::MakeRotateYMatrix(float radian)

{

	Matrix4x4 ans;
	ans.m[0][0] = std::cos(radian);
	ans.m[0][1] = 0;
	ans.m[0][2] = -std::sin(radian);
	ans.m[0][3] = 0;

	ans.m[1][0] = 0;
	ans.m[1][1] = 1;
	ans.m[1][2] = 0;
	ans.m[1][3] = 0;

	ans.m[2][0] = std::sin(radian);;
	ans.m[2][1] = 0;
	ans.m[2][2] = std::cos(radian);;
	ans.m[2][3] = 0;

	ans.m[3][0] = 0;
	ans.m[3][1] = 0;
	ans.m[3][2] = 0;
	ans.m[3][3] = 1;

	return ans;

}


Matrix4x4 MyMath::MakeRotateZMatrix(float radian)

{
	Matrix4x4 ans;
	ans.m[0][0] = std::cos(radian);
	ans.m[0][1] = std::sin(radian);
	ans.m[0][2] = 0;
	ans.m[0][3] = 0;

	ans.m[1][0] = -std::sin(radian);
	ans.m[1][1] = std::cos(radian);
	ans.m[1][2] = 0;
	ans.m[1][3] = 0;

	ans.m[2][0] = 0;
	ans.m[2][1] = 0;
	ans.m[2][2] = 1;
	ans.m[2][3] = 0;

	ans.m[3][0] = 0;
	ans.m[3][1] = 0;
	ans.m[3][2] = 0;
	ans.m[3][3] = 1;


	return ans;

}


Matrix4x4 MyMath::MakeRotateMatrix(const Vector3& rotate)
{

	Matrix4x4 rotateX = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateY = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZ = MakeRotateZMatrix(rotate.z);
	return rotateX * rotateY * rotateZ;
}

Matrix4x4 MyMath::MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate)
{

	Matrix4x4 rotateXYZ = MakeRotateXMatrix(rotate.x) * MakeRotateYMatrix(rotate.y) * MakeRotateZMatrix(rotate.z);
	return MakeScaleMatrix(scale) * rotateXYZ * MakeTranslateMatrix(translate);

}

float MyMath::Cot(float theta)
{
	return 1 / std::tan(theta);
}

float MyMath::Dot(const Vector3& v1, const Vector3& v2)
{

	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

float MyMath::Dot(const Vector3& v1, float num)
{

	return v1.x * num + v1.y * num + v1.z * num;
}

float MyMath::Dot(const Quaternion& q1, const Quaternion& q2)
{
	return q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;

}

float MyMath::Length(const Vector3& v)
{
	float ans;

	ans = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	return ans;
}



Vector3 MyMath::Lerp(const Vector3& v1, const Vector3& v2, float t)
{
	
	return v1 + (v2 - v1) * t;

}

Matrix4x4 MyMath::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
	float cotHalfFov = Cot(fovY / 2);
	float depthRange = farClip - nearClip;
	return MakeMatrixFromRows(
		{ cotHalfFov / aspectRatio, 0.0f, 0.0f, 0.0f },
		{ 0.0f, cotHalfFov, 0.0f, 0.0f },
		{ 0.0f, 0.0f, farClip / depthRange, 1.0f },
		{ 0.0f, 0.0f, -(nearClip * farClip) / depthRange, 0.0f });
}
Matrix4x4 MyMath::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
	return MakeMatrixFromRows(
		{ 2 / (right - left), 0.0f, 0.0f, 0.0f },
		{ 0.0f, 2 / (top - bottom), 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1 / (farClip - nearClip), 0.0f },
		{ (left + right) / (left - right), (top + bottom) / (bottom - top), nearClip / (nearClip - farClip), 1.0f });
}

Matrix4x4 MyMath::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth)
{
	return MakeMatrixFromRows(
		{ width / 2, 0.0f, 0.0f, 0.0f },
		{ 0.0f, -(height / 2), 0.0f, 0.0f },
		{ 0.0f, 0.0f, maxDepth - minDepth, 0.0f },
		{ left + (width / 2), top + (height / 2), minDepth, 1.0f });
}

//単位行列
Matrix4x4 MyMath::MakeIdentity4x4() {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return result;
}

Matrix4x4 MyMath::Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			for (int i = 0; i < 4; ++i) {
				result.m[row][column] += m1.m[row][i] * m2.m[i][column];
			}
		}
	}
	return result;
}

Matrix4x4 MyMath::Transpose(const Matrix4x4& matrix)
{
	
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = matrix.m[j][i];
		}
	}
	return result;
}

Vector3 MyMath::Project(const Vector3& v1, const Vector3& v2)
{
	float t = Dot(v1, v2) / Dot(v2, v2);
	Vector3 tb = { v2.x * t,v2.y * t,v2.z * t };
	return tb;
}

Vector3 MyMath::ClosestPoint(const Vector3& point, const Segment& segment)
{
	Vector3 v = point - segment.origin;
	float t = Dot(v, segment.diff) / Dot(segment.diff, segment.diff);
	t = std::clamp(t, 0.0f, 1.0f);

	Vector3 tb = { t * segment.diff.x, t * segment.diff.y,t * segment.diff.z };
	return Add(segment.origin, tb);
}

bool MyMath::IsCollision(const Sphere& s1, const Sphere& s2)
{
	float distance = Length(s2.center - s1.center);

	if (distance <= s1.radius + s2.radius) { return true; };

	return false;
}

bool MyMath::IsCollision(const Sphere& s1, const Plane& plane)
{
	float d = Dot(plane.normal, plane.distance);
	float k = fabs(Dot(plane.normal, s1.center) - d);
	if (k <= s1.radius) {
		return true;
	}
	return false;
}

bool MyMath::IsCollision(const Segment& segment, const Plane& plane)
{
	float dot = Dot(plane.normal, segment.diff);
	if (dot == 0.0f) {
		return false;
	}
	float t = (plane.distance - Dot(segment.origin, plane.normal)) / dot;

	if (0 <= t && t <= 1) { return true; }
	return false;
}

bool MyMath::IsCollision(const Segment& segment, const Triangle& triangle)
{
	Vector3 edge01 = triangle.vertices[0] - triangle.vertices[1];
	Vector3 edge12 = triangle.vertices[2] - triangle.vertices[1];
	Vector3 edge20 = triangle.vertices[0] - triangle.vertices[2];
	Plane plane;
	plane.normal = Normalize(edge01.Cross(edge12));
	plane.distance = Dot(plane.normal, triangle.vertices[0]);

	float dot = Dot(plane.normal, segment.diff);
	if (dot == 0.0f) {
		return false;
	}
	float t = (plane.distance - Dot(segment.origin, plane.normal)) / dot;
	if (t < 0.0f || t > 1.0f) {
		return false;
	}

	Vector3 collisionPoint = segment.origin + segment.diff * t;
	return IsPointInsideTriangleOnPlane(collisionPoint, triangle, plane, edge01, edge12, edge20);
}

bool MyMath::IsCollision(const AABB& aabb1, const AABB& aabb2)
{
	if ((aabb1.min.x <= aabb2.max.x && aabb1.max.x >= aabb2.min.x) &&
		(aabb1.min.y <= aabb2.max.y && aabb1.max.y >= aabb2.min.y) &&
		(aabb1.min.z <= aabb2.max.z && aabb1.max.z >= aabb2.min.z)) {

		return true;


	}

	return false;
}

bool MyMath::IsCollision(const AABB& aabb, const Sphere& sphere)
{
	Vector3 closestPoint = ClampPointToAabb(sphere.center, aabb);
	float distance = Length(closestPoint - sphere.center);
	return distance <= sphere.radius;
}

bool MyMath::IsCollision(const AABB& aabb, const Segment& segment)
{
	Vector3 tNear{};
	Vector3 tFar{};
	CalculateAabbSegmentParamRange(aabb, segment, tNear, tFar);
	float tmin = std::max(std::max(tNear.x, tNear.y), tNear.z);
	float tmax = std::min(std::min(tFar.x, tFar.y), tFar.z);
	return tmin <= tmax && tmin * tmax < 0.0f;
}

bool MyMath::IsCollision(const OBB& obb1, const OBB& obb2)
{
	constexpr float kEpsilon = 0.0001f;
	float rotation[3][3]{};
	float absRotation[3][3]{};

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			rotation[i][j] = Dot(obb1.orientations[i], obb2.orientations[j]);
			absRotation[i][j] = std::abs(rotation[i][j]) + kEpsilon;
		}
	}

	const Vector3 centerDiff = obb2.center - obb1.center;
	const float translation[3]{
		Dot(centerDiff, obb1.orientations[0]),
		Dot(centerDiff, obb1.orientations[1]),
		Dot(centerDiff, obb1.orientations[2]),
	};
	const float a[3]{ obb1.size.x, obb1.size.y, obb1.size.z };
	const float b[3]{ obb2.size.x, obb2.size.y, obb2.size.z };

	for (int i = 0; i < 3; ++i) {
		const float ra = a[i];
		const float rb = b[0] * absRotation[i][0] + b[1] * absRotation[i][1] + b[2] * absRotation[i][2];
		if (std::abs(translation[i]) > ra + rb) {
			return false;
		}
	}

	for (int j = 0; j < 3; ++j) {
		const float ra = a[0] * absRotation[0][j] + a[1] * absRotation[1][j] + a[2] * absRotation[2][j];
		const float rb = b[j];
		const float projectedTranslation =
			translation[0] * rotation[0][j] + translation[1] * rotation[1][j] + translation[2] * rotation[2][j];
		if (std::abs(projectedTranslation) > ra + rb) {
			return false;
		}
	}

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			const int i1 = (i + 1) % 3;
			const int i2 = (i + 2) % 3;
			const int j1 = (j + 1) % 3;
			const int j2 = (j + 2) % 3;
			const float ra = a[i1] * absRotation[i2][j] + a[i2] * absRotation[i1][j];
			const float rb = b[j1] * absRotation[i][j2] + b[j2] * absRotation[i][j1];
			const float projectedTranslation =
				translation[i2] * rotation[i1][j] - translation[i1] * rotation[i2][j];
			if (std::abs(projectedTranslation) > ra + rb) {
				return false;
			}
		}
	}

	return true;
}

Matrix4x4 MyMath::MakeRotateAxisAngle(const Vector3& axis, float angle)
{
	float cosA = cosf(angle);
	float sinA = sinf(angle);
	float oneMinusCosA = 1.0f - cosA;

	float x = axis.x;
	float y = axis.y;
	float z = axis.z;
	return MakeMatrixFromRows(
		{ x * x * oneMinusCosA + cosA, y * x * oneMinusCosA - z * sinA, z * x * oneMinusCosA + y * sinA, 0.0f },
		{ x * y * oneMinusCosA + z * sinA, y * y * oneMinusCosA + cosA, z * y * oneMinusCosA - x * sinA, 0.0f },
		{ x * z * oneMinusCosA - y * sinA, y * z * oneMinusCosA + x * sinA, z * z * oneMinusCosA + cosA, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f });
}

Matrix4x4 MyMath::DirectionToDirection(const Vector3& from, const Vector3& to)
{
	Vector3 f = MyMath::Normalize(from);
	Vector3 t = MyMath::Normalize(to);
	float dot = MyMath::Dot(f, t);

	// ほぼ同じ方向なら単位行列
	if (dot > 0.9999f) {
		return MakeIdentity4x4();
	}

	// 真逆方向（180度）なら適当な直交軸を使って180度回転
	if (dot < -0.9999f) {
		Vector3 ortho = SelectOrthogonalAxis(f);
		Vector3 axis = MyMath::Cross(f, ortho);
		axis = MyMath::Normalize(axis);
		return MakeRotateAxisAngle(axis, std::numbers::pi_v<float>);
	}

	// 通常回転
	Vector3 axis = MyMath::Cross(f, t);
	axis = MyMath::Normalize(axis);
	float angle = std::acos(std::clamp(dot, -1.0f, 1.0f)); // NaN対策

	return MakeRotateAxisAngle(axis, angle);
}

Matrix4x4 MyMath::MakeRotationMatrix(const Quaternion& Quaternion)
{
	float x2 = Quaternion.x + Quaternion.x;
	float y2 = Quaternion.y + Quaternion.y;
	float z2 = Quaternion.z + Quaternion.z;
	float xx = Quaternion.x * x2;
	float xy = Quaternion.x * y2;
	float xz = Quaternion.x * z2;
	float yy = Quaternion.y * y2;
	float yz = Quaternion.y * z2;
	float zz = Quaternion.z * z2;
	float wx = Quaternion.w * x2;
	float wy = Quaternion.w * y2;
	float wz = Quaternion.w * z2;
	return Matrix4x4{
		1.0f - (yy + zz), xy + wz, xz - wy, 0.0f,
		xy - wz, 1.0f - (xx + zz), yz + wx, 0.0f,
		xz + wy, yz - wx, 1.0f - (xx + yy), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
}

Matrix4x4 MyMath::MakeAffineMatrix(const Vector3& scale, const Quaternion& quaternion, const Vector3& translate)
{
	Matrix4x4 rotate = MakeRotationMatrix(quaternion);
	return MakeScaleMatrix(scale) * rotate * MakeTranslateMatrix(translate);
}

Quaternion MyMath::Slerp(const Quaternion& start, const Quaternion& end, float t)
{
	// クォータニオンの内積を計算
	float dot = start.x * end.x + start.y * end.y + start.z * end.z + start.w * end.w;
	// 内積が負の場合は、片方のクォータニオンを反転
	Quaternion endQuat = end;
	if (dot < 0.0f) {
		endQuat = { -end.x, -end.y, -end.z, -end.w };
		dot = -dot;
	}
	// 内積が 1 に近い場合は、線形補間
	if (dot > 0.9995f) {
		return Normalize(start + (endQuat - start) * t);
	}
	// クォータニオンの角度を計算
	float theta = acosf(dot);
	// クォータニオンの正規化
	float sinTheta = sinf(theta);
	float sinTTheta = sinf(t * theta);
	float sinOneMinusTTheta = sinf((1.0f - t) * theta);
	return (start * sinOneMinusTTheta + endQuat * sinTTheta) / sinTheta;

}

Quaternion MyMath::Normalize(const Quaternion& quaternion)
{
	
	float length = sqrtf(quaternion.x * quaternion.x + quaternion.y * quaternion.y + quaternion.z * quaternion.z + quaternion.w * quaternion.w);
	if (length == 0.0f) {
		return { 0.0f, 0.0f, 0.0f, 1.0f }; // ゼロ除算を避けるためのデフォルト値
	}
	return { quaternion.x / length, quaternion.y / length, quaternion.z / length, quaternion.w / length };
}

Vector3 MyMath::GetTranslate(const Matrix4x4& matrix)
{
	
	Vector3 ans;
	ans.x = matrix.m[3][0];
	ans.y = matrix.m[3][1];
	ans.z = matrix.m[3][2];
	return ans;

}

void MyMath::MatrixImGuiText(const Matrix4x4& matrix, const char* label)
{

	// ラベルを表示
	ImGui::Text("%s", label);
	// 行列の各成分を ImGui テキストで表示
	for (int i = 0; i < 4; i++) {
		ImGui::Text("%.03f %.03f %.03f %.03f", matrix.m[i][0], matrix.m[i][1], matrix.m[i][2], matrix.m[i][3]);
	}

}

void MyMath::QuaternionImGuiText(const Quaternion& quaternion, const char* label)
{
	// クォータニオンの各成分を ImGui テキストで表示
	ImGui::Text("x: %.02f", quaternion.x);
	ImGui::SameLine();
	ImGui::Text("y: %.02f", quaternion.y);
	ImGui::SameLine();
	ImGui::Text("z: %.02f", quaternion.z);
	ImGui::SameLine();
	ImGui::Text("w: %.02f", quaternion.w);

	// ラベルを表示
	ImGui::SameLine();
	ImGui::Text("%s", label);
}

void MyMath::Vector3ImGuiText(const Vector3& vector, const char* label)
{

	// ラベルを表示
	ImGui::Text("%s", label);
	// ベクトルの各成分を ImGui テキストで表示
	ImGui::Text("x: %.02f", vector.x);
	ImGui::SameLine();
	ImGui::Text("y: %.02f", vector.y);
	ImGui::SameLine();
	ImGui::Text("z: %.02f", vector.z);

}




}




