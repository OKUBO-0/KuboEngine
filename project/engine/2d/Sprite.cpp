#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "Matrix4x4.h"
#include <MyMath.h>
#include <CameraManager.h>

namespace Engine::Graphics2D {

void Sprite::Initialize(SpriteCommon* spriteCommon, const std::string& textureFilePath)
{
    textureFilePath_ = textureFilePath;
    spriteCommon_ = spriteCommon;
    Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath_);
    CreateGpuResources();
    InitializeBufferViews();
    InitializeMaterialData();
    InitializeTransformationData();
    InitializeCameraData();
    AdjustTextureSize();
}

void Sprite::Update()
{
    UpdateCameraData();
    UpdateVertexData();
    UpdateIndexData();
    UpdateMatrices();
}

void Sprite::Draw()
{
    // 頂点バッファ設定
    spriteCommon_->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    spriteCommon_->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView);

    // マテリアルCBV設定 (RootParameter[0])
    spriteCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

    // テクスチャSRV設定 (RootParameter[1])
    spriteCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootDescriptorTable(1, Engine::Base::TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));

    // 行列CBV設定 (RootParameter[2])
    spriteCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(2, transformationMatrixResource->GetGPUVirtualAddress());

    // インデックス付き描画
    spriteCommon_->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
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

void Sprite::CreateGpuResources()
{
    vertexResource = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * 4);
    indexResource = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(uint32_t) * 6);
    materialResource = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(MaterialSprite));
    transformationMatrixResource = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrixsprite));
}

void Sprite::InitializeBufferViews()
{
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    indexBufferView.BufferLocation = indexResource->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

void Sprite::InitializeMaterialData()
{
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData->uvTransform = materialData->uvTransform.MakeIdentity4x4();
}

void Sprite::InitializeTransformationData()
{
    transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
    transformationMatrixData_->WVP = transformationMatrixData_->WVP.MakeIdentity4x4();
    transformationMatrixData_->World = transformationMatrixData_->World.MakeIdentity4x4();
}

void Sprite::InitializeCameraData()
{
    cameraResource = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(CaMeraForGpu));
    cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraForGpu));
    cameraForGpu->worldPosition = { 0.0f,0.0f,0.0f };
}

void Sprite::UpdateCameraData()
{
    Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
    if (activeCamera) {
        cameraForGpu->worldPosition = activeCamera->GetTransform().translate;
    }
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

    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    vertexData[0].position = { left, bottom, 0.0f, 1.0f };
    vertexData[1].position = { left, top, 0.0f, 1.0f };
    vertexData[2].position = { right, bottom, 0.0f, 1.0f };
    vertexData[3].position = { right, top, 0.0f, 1.0f };
    vertexData[0].texcoord = { texLeft, texBottom };
    vertexData[1].texcoord = { texLeft, texTop };
    vertexData[2].texcoord = { texRight, texBottom };
    vertexData[3].texcoord = { texRight, texTop };
    vertexData[0].normal = { 0.0f,0.0f,-1.0f };
    vertexData[1].normal = { 0.0f,0.0f,-1.0f };
    vertexData[2].normal = { 0.0f,0.0f,-1.0f };
    vertexData[3].normal = { 0.0f,0.0f,-1.0f };
}

void Sprite::UpdateIndexData()
{
    indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    indexData[0] = 0;
    indexData[1] = 1;
    indexData[2] = 2;
    indexData[3] = 1;
    indexData[4] = 3;
    indexData[5] = 2;
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
    transformationMatrixData_->WVP = worldViewProjectionMatrix;
    transformationMatrixData_->World = worldMatrix;
}

}
