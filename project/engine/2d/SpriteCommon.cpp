#include "SpriteCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"

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
}

void SpriteCommon::Finalize()
{
    graphicsPipeline_.reset();
    dxCommon_.reset();
}

void SpriteCommon::CommonDraw()
{
    const auto dxCommon = dxCommon_.lock();
    if (!dxCommon) {
        return;
    }
    // RootSignatureとPSOを設定し、プリミティブトポロジを三角形リストに指定
    const auto rootSignature = graphicsPipeline_->GetRootSignatureSpriteHandle();
    const auto pipelineState = graphicsPipeline_->GetGraphicsPipelineStateSpriteHandle();
    dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
    dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
    dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

}
