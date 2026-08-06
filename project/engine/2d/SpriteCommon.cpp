#include "SpriteCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include <array>

namespace Engine::Graphics2D {

SpriteCommon::~SpriteCommon() = default;

SpriteCommon* SpriteCommon::GetInstance()
{
    static SpriteCommon instance;
    return &instance;
}

void SpriteCommon::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon)
{
    dxCommon_ = dxCommon;
    // グラフィックスパイプライン生成
    graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
    graphicsPipeline_->Initialize(dxCommon);
    graphicsPipeline_->CreateSprite();
    InitializeSharedBuffers();
}

void SpriteCommon::Finalize()
{
    sharedIndexBuffer_.Reset();
    graphicsPipeline_.reset();
    dxCommon_.reset();
}

void SpriteCommon::CommonDraw()
{
    const auto dxCommon = dxCommon_.lock();
    if (!dxCommon) {
        return;
    }
    lastDrawStats_ = currentDrawStats_;
    currentDrawStats_ = {};
    // RootSignatureとPSOを設定し、プリミティブトポロジを三角形リストに指定
    const auto rootSignature = graphicsPipeline_->GetRootSignatureSpriteHandle();
    const auto pipelineState = graphicsPipeline_->GetGraphicsPipelineStateSpriteHandle();
    dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
    dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
    dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpriteCommon::RecordSpriteDraw(uint32_t uploadedBytes)
{
    ++currentDrawStats_.drawCallCount;
    currentDrawStats_.uploadedBytes += uploadedBytes;
}

void SpriteCommon::InitializeSharedBuffers()
{
    const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
        dxCommon_.lock();
    if (!dxCommon) {
        return;
    }

    constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 1, 3, 2 };
    sharedIndexBuffer_ = dxCommon->CreateDefaultBufferResource(
        indices.data(),
        sizeof(uint32_t) * indices.size(),
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    sharedIndexBufferView_ = {
        .BufferLocation = sharedIndexBuffer_->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indices.size()),
        .Format = DXGI_FORMAT_R32_UINT,
    };
}

}
