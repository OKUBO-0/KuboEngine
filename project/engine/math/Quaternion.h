#pragma once
#include <iostream>

struct Quaternion {
    float x, y, z, w;

    // 加算演算子のオーバーロード
    Quaternion operator+(const Quaternion& q) const {
        return { x + q.x, y + q.y, z + q.z, w + q.w };
    }

    // 減算演算子のオーバーロード
    Quaternion operator-(const Quaternion& q) const {
        return { x - q.x, y - q.y, z - q.z, w - q.w };
    }

    // 乗算演算子のオーバーロード
    Quaternion operator*(const Quaternion& q) const {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        };
    }

    // スカラー乗算演算子のオーバーロード
    Quaternion operator*(float scalar) const {
        return { x * scalar, y * scalar, z * scalar, w * scalar };
    }

    // スカラー除算演算子のオーバーロード
    Quaternion operator/(float scalar) const {
        return { x / scalar, y / scalar, z / scalar, w / scalar };
    }

    // 出力演算子のオーバーロード
    friend std::ostream& operator<<(std::ostream& os, const Quaternion& q) {
        os << "Quaternion(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
        return os;
    }
};