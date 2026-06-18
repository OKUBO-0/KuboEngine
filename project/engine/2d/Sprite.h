#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "RenderingData.h"

#include <array>
#include <string>
#include <wrl/client.h>
#include <d3d12.h>

namespace Engine::Graphics2D {

class SpriteCommon;
/// @brief 2Dスプライト1枚分の描画情報を管理するクラス
/// @details テクスチャ、サイズ、座標、UV、色を保持し、更新と描画を行う。
class Sprite
{
public:
    /// 初期化処理（共通設定とテクスチャ読み込み）
    void Initialize(SpriteCommon* spriteCommon, const std::string& textureFilePath);
    void SetTexture(const std::string& textureFilePath);

    /// 毎フレーム更新（座標・回転・UVなど）
    void Update();

    /// 描画処理（バッファ設定と描画コマンド発行）
    void Draw();

    // サイズ
    const Vector2& GetSize() const { return size; }
    void SetSize(const Vector2& size) { this->size = size; }

    // 位置
    const Vector2& GetPosition() const { return position; }
    void SetPosition(const Vector2& position) { this->position = position; }

    // 回転角度
    float GetRotation() const { return rotation; }
    void SetRotation(float rotation) { this->rotation = rotation; }

    // 上辺をスプライト幅に対する割合で横へずらす
    float GetSkewX() const { return skewX_; }
    void SetSkewX(float skewX) { skewX_ = skewX; }

    // 色（マテリアルカラー）
    const Vector4& GetColor() const { return materialData_.color; }
    void SetColor(const Vector4& color) { materialData_.color = color; }

    // アンカーポイント（基準位置）
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }
    void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }

    // 左右反転
    bool GetIsFlipX() const { return isFlipX_; }
    void SetIsFlipX(bool isFlipX) { isFlipX_ = isFlipX; }

    // 上下反転
    bool GetIsFlipY() const { return isFlipY_; }
    void SetIsFlipY(bool isFlipY) { isFlipY_ = isFlipY; }

    // テクスチャ左上座標
    const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
    void SetTextureLeftTop(const Vector2& textureLeftTop) { textureLeftTop_ = textureLeftTop; }

    // テクスチャ切り出しサイズ
    const Vector2& GetTextureSize() const { return textureSize_; }
    void SetTextureSize(const Vector2& textureSize) { textureSize_ = textureSize; }

    // デバッグ表示名
    void SetDebugName(const std::string& debugName) { debugName_ = debugName; }
    const std::string& GetDebugName() const { return debugName_; }

private:
    std::string textureFilePath_;
    std::string debugName_;

    /// テクスチャサイズを画像に合わせる
    void AdjustTextureSize();
    void InitializeMaterialData();
    void InitializeTransformationData();
    void UpdateVertexData();
    void UpdateIndexData();
    void UpdateMatrices();
    D3D12_GPU_VIRTUAL_ADDRESS UploadFrameConstant(const void* data, size_t size);

    SpriteCommon* spriteCommon_ = nullptr;

    std::array<VertexData, 4> vertexData_{};
    std::array<uint32_t, 6> indexData_{};
    MaterialSprite materialData_{};
    TransformationMatrixsprite transformationMatrixData_{};

    // 変換情報（スケール・回転・平行移動）
    EulerTransform transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

    // 設定用パラメータ
    Vector2 size = { 640.0f,360.0f };
    Vector2 position = { 0.0f,0.0f };
    float rotation = 0.0f;
    float skewX_ = 0.0f;
    uint32_t textureIndex = 0;

    Vector2 anchorPoint_ = { 0.0f,0.0f }; // アンカーポイント
    bool isFlipX_ = false;                // 左右反転
    bool isFlipY_ = false;                // 上下反転

    Vector2 textureLeftTop_ = { 0.0f,0.0f };   // テクスチャ左上座標
    Vector2 textureSize_ = { 512.0f,512.0f };  // テクスチャ切り出しサイズ

    // 行列
    Matrix4x4 worldMatrix;
    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;
    Matrix4x4 worldViewProjectionMatrix;

};

}
