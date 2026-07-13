#include "Sprite.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"
#include <MyMath.h>
#include <cstring>
#include <memory>

namespace Engine::Graphics2D {

void Sprite::Initialize(SpriteCommon* spriteCommon, const std::string& textureFilePath)
{
    textureFilePath_ = textureFilePath;
    spriteCommon_ = spriteCommon;
    Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath_);
    InitializeMaterialData();
    InitializeTransformationData();
    AdjustTextureSize();
}

void Sprite::SetTexture(const std::string& textureFilePath)
{
    textureFilePath_ = textureFilePath;
    Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath_);
    const DirectX::TexMetadata& metadata = Engine::Base::TextureManager::GetInstance()->GetMetaData(textureFilePath_);
    textureLeftTop_ = { 0.0f, 0.0f };
    textureSize_ = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };
}

void Sprite::Update()
{
    UpdateVertexData();
    UpdateIndexData();
    UpdateMatrices();
}

void Sprite::Draw()
{
    const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
        spriteCommon_->GetDxCommon();
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
        UploadFrameConstant(&materialData_, sizeof(materialData_));
    const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
        UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
    const Engine::Base::DirectXCommon::FrameUploadAllocation vertexAllocation =
        dxCommon->AllocateFrameUpload(
            sizeof(vertexData_),
            alignof(VertexData));
    std::memcpy(
        vertexAllocation.cpuAddress,
        vertexData_.data(),
        sizeof(vertexData_));
    const Engine::Base::DirectXCommon::FrameUploadAllocation indexAllocation =
        dxCommon->AllocateFrameUpload(
            sizeof(indexData_),
            alignof(uint32_t));
    std::memcpy(
        indexAllocation.cpuAddress,
        indexData_.data(),
        sizeof(indexData_));
    const D3D12_VERTEX_BUFFER_VIEW vertexBufferView{
        .BufferLocation = vertexAllocation.gpuAddress,
        .SizeInBytes = static_cast<UINT>(sizeof(vertexData_)),
        .StrideInBytes = sizeof(VertexData),
    };
    const D3D12_INDEX_BUFFER_VIEW indexBufferView{
        .BufferLocation = indexAllocation.gpuAddress,
        .SizeInBytes = static_cast<UINT>(sizeof(indexData_)),
        .Format = DXGI_FORMAT_R32_UINT,
    };

    // 頂点バッファ設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);

    // マテリアルCBV設定 (RootParameter[0])
    commandList->SetGraphicsRootConstantBufferView(0, materialAddress);

    // テクスチャSRV設定 (RootParameter[1])
    commandList->SetGraphicsRootDescriptorTable(1, Engine::Base::TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));

    // 行列CBV設定 (RootParameter[2])
    commandList->SetGraphicsRootConstantBufferView(2, transformAddress);

    // インデックス付き描画
    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::AdjustTextureSize()
{
    // テクスチャメタデータを取得
    const DirectX::TexMetadata& metadata = Engine::Base::TextureManager::GetInstance()->GetMetaData(textureFilePath_);

    // 切り出しサイズをテクスチャ全体に設定
    textureSize_ = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };

    // スプライトサイズをテクスチャサイズに合わせる
    size = textureSize_;
}

void Sprite::InitializeMaterialData()
{
    materialData_.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_.uvTransform = materialData_.uvTransform.MakeIdentity4x4();
}

void Sprite::InitializeTransformationData()
{
    transformationMatrixData_.WVP =
        transformationMatrixData_.WVP.MakeIdentity4x4();
    transformationMatrixData_.World =
        transformationMatrixData_.World.MakeIdentity4x4();
}

void Sprite::UpdateVertexData()
{
    float left = 0.0f - anchorPoint_.x;
    float right = 1.0f - anchorPoint_.x;
    float top = 0.0f - anchorPoint_.y;
    float bottom = 1.0f - anchorPoint_.y;
    if (isFlipX_) {
        left = -left;
        right = -right;
    }
    if (isFlipY_) {
        top = -top;
        bottom = -bottom;
    }

    const DirectX::TexMetadata& metadata = Engine::Base::TextureManager::GetInstance()->GetMetaData(textureFilePath_);
    const float texLeft = textureLeftTop_.x / metadata.width;
    const float texRight = (textureLeftTop_.x + textureSize_.x) / metadata.width;
    const float texTop = textureLeftTop_.y / metadata.height;
    const float texBottom = (textureLeftTop_.y + textureSize_.y) / metadata.height;

    vertexData_[0].position = { left, bottom, 0.0f, 1.0f };
    vertexData_[1].position = { left + skewX_, top, 0.0f, 1.0f };
    vertexData_[2].position = { right, bottom, 0.0f, 1.0f };
    vertexData_[3].position = { right + skewX_, top, 0.0f, 1.0f };
    vertexData_[0].texcoord = { texLeft, texBottom };
    vertexData_[1].texcoord = { texLeft, texTop };
    vertexData_[2].texcoord = { texRight, texBottom };
    vertexData_[3].texcoord = { texRight, texTop };
    vertexData_[0].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[1].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[2].normal = { 0.0f,0.0f,-1.0f };
    vertexData_[3].normal = { 0.0f,0.0f,-1.0f };
}

void Sprite::UpdateIndexData()
{
    indexData_ = { 0, 1, 2, 1, 3, 2 };
}

void Sprite::UpdateMatrices()
{
    transform.rotate = { 0.0f,0.0f,rotation };
    transform.translate = { position.x,position.y,0.0f };
    transform.scale = { size.x,size.y,1.0f };
    worldMatrix = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	projectionMatrix = MyMath::MakeOrthographicMatrix(
        0.0f, 0.0f, float(Engine::Base::WinApp::kClientWidth), float(Engine::Base::WinApp::kClientHeight), 0.0f, 100.0f);
    worldViewProjectionMatrix = worldMatrix * viewMatrix.MakeIdentity4x4() * projectionMatrix;
    transformationMatrixData_.WVP = worldViewProjectionMatrix;
    transformationMatrixData_.World = worldMatrix;
}

D3D12_GPU_VIRTUAL_ADDRESS Sprite::UploadFrameConstant(
    const void* data,
    size_t size)
{
    const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
        spriteCommon_->GetDxCommon();
    const Engine::Base::DirectXCommon::FrameUploadAllocation allocation =
        dxCommon->AllocateFrameUpload(size, 256);
    std::memcpy(allocation.cpuAddress, data, size);
    return allocation.gpuAddress;
}

}
