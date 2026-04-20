#include "SpriteCommon.h"

namespace Engine::Graphics2D {

SpriteCommon* SpriteCommon::GetInstance()
{
    static SpriteCommon instance;
    return &instance;
}

void SpriteCommon::Initialize(Engine::Base::DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    // グラフィックスパイプライン生成
    graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
    graphicsPipeline_->Initialize(dxCommon_);
    graphicsPipeline_->CreateSprite();
}

void SpriteCommon::Finalize()
{
    graphicsPipeline_.reset();
    dxCommon_ = nullptr;
}

void SpriteCommon::CommonDraw()
{
    // RootSignatureとPSOを設定し、プリミティブトポロジを三角形リストに指定
    dxCommon_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureSprite());
    dxCommon_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateSprite());
    dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

}
