#pragma once
#include <corecrt_math.h>

/// @brief 3 次元ベクトル
/// @details 座標、方向、スケールなどに使う基本演算付きの値オブジェクト。
namespace Engine::Math {

struct Vector3 final {

	float x;
	float y;
	float z;


    Vector3() :x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) {
        this->x = x;
        this->y = y;
        this->z = z;
    }

    Vector3 operator+(const Vector3& other) const {
        return Vector3{ x + other.x, y + other.y, z + other.z };
    }
    Vector3 operator-(const Vector3& other) const {
        return Vector3{ x - other.x, y - other.y, z - other.z };
    }

    Vector3 operator*(int other) const {
        return Vector3{ x * other,y * other,z * other };
    }
    Vector3 operator*(float other) const {
        return Vector3{ x * other,y * other,z * other };
    }
    Vector3 operator/(int other) const {
        return Vector3{ x / other, y / other, z / other };
    }
    Vector3 operator/(float other) const {
        return Vector3{ x / other, y / other, z / other };
    }
    Vector3 operator-(float other) const {
        return Vector3{ x - other, y - other, z - other };
    }

    Vector3 operator+(float other) const {
        return Vector3{ x + other, y + other, z + other };
    }

    void operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
    }

    void operator-=(const Vector3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
    }

    float Dot(const Vector3& v) const {
        return x * v.x + y * v.y + z * v.z;
    }
    Vector3 Cross(const Vector3& v) const {
        return { y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x };
    }

    float Length() const {
        return sqrtf(x * x + y * y + z * z);
    }
    Vector3 Normalize() const {
        return *this / Length();
    }

    void operator/=(const int other) {
        *this = *this / other;
    }

    Vector3& operator*=(float other) {
        this->x *= other;
        this->y *= other;
        this->z *= other;
        return *this;
    }


};

}

using Engine::Math::Vector3;

