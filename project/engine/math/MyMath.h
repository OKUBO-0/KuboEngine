#pragma once
#include "Matrix4x4.h"
#include "Matrix3x3.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Vector2.h"
#include "Quaternion.h"
#pragma once
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
struct Sphere {
	Vector3 center;
	float radius;
};



struct Ray {

	Vector3 origin;
	Vector3 diff;
};

struct Segment {

	Vector3 origin;
	Vector3 diff;
};

struct Plane {

	Vector3 normal;
	float distance;

};

struct Triangle {

	Vector3 vertices[3];
};

struct AABB {

	Vector3 min;
	Vector3 max;
};



namespace MyMath {
	//回転
	Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
	//拡大
	Matrix4x4 MakeScaleMatrix(const Vector3& scale);
	//同時座標変換
	Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);
	Vector3 Normalize(const Vector3& vector);

	//Add
	Vector3 Add(const Vector3& v, float scalar);
	Vector3 Add(const Vector3& v1, const Vector3& v2);

	//cross
	Vector3 Cross(const Vector3& v1, const Vector3& v2);

	//回転X
	Matrix4x4 MakeRotateXMatrix(float radian);
	//回転Y
	Matrix4x4 MakeRotateYMatrix(float radian);
	//回転Z
	Matrix4x4 MakeRotateZMatrix(float radian);

	//回転XYZ
	Matrix4x4 MakeRotateMatrix(const Vector3& rotate);

	Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

	float Cot(float theta);
	//ドット積
	float Dot(const Vector3& v1, const Vector3& v2);
	float Dot(const Vector3& v1, const float& num);
	float Dot(const Quaternion& q1, const Quaternion& q2);
	float Length(const Vector3& v);
	

	//Lerp
	Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);

	//透視投影行列
	Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	//正射影行列
	Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottm, float nearCip, float farCip);
	//ビューポート変換行列
	Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);
	//単位行列
	Matrix4x4 MakeIdentity4x4();
	Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

	Matrix4x4 Transpose(const Matrix4x4& matrix);


	//最近接点
	Vector3 Project(const Vector3& v1, const Vector3& v2);
	Vector3 ClosestPoint(const Vector3& point, const Segment& segment);
	//当たり判定
	bool IsCollision(const Sphere& s1, const Sphere& s2);
	bool IsCollision(const Sphere& s1, const Plane& plane);
	bool IsCollision(const Segment& segment, const Plane& plane);
	bool IsCollision(const Segment& segment, const Triangle& triangle);
	bool IsCollision(const AABB& aabb1, const AABB& aabb2);
	bool IsCollision(const AABB& aabb, const Sphere& sphere);
	bool IsCollision(const AABB& aabb, const Segment& segment);

	//ベクトルを求める関数
	Matrix4x4 MakeRotateAxisAngle(const Vector3& axis, float angle);
	//ある方向をある方向に向ける回転行列
	Matrix4x4 DirectionToDirection(const Vector3& from, const Vector3& to);

	//クオタニオン
	//クオタニオンを行列に変換
	Matrix4x4 MakeRotationMatrix(const Quaternion& Quaternion);
	//クオタニオンを回転行列に変換
	Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& Quaternion, const Vector3& translate);
	//球面線形補間
	Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t);

	//クオタニオンの正規化
	Quaternion Normalize(const Quaternion& quaternion);

	Vector3 GetTranslate( const Matrix4x4& matrix);

	//debugテキスト
	void MatrixImGuiText(const Matrix4x4& matrix, const char* label);
	void QuaternionImGuiText(const Quaternion& quaternion, const char* label);
	void Vector3ImGuiText(const Vector3& vector, const char* label);
}














