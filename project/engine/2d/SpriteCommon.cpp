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
    dxCommonRaw_ = dxCommon.get();
    // グラフィックスパイプライン生成
    graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
    graphicsPipeline_->Initialize(dxCommonRaw_);
    graphicsPipeline_->CreateSprite();
}

void SpriteCommon::Finalize()
{
    graphicsPipeline_.reset();
    dxCommon_.reset();
    dxCommonRaw_ = nullptr;
}

void SpriteCommon::CommonDraw()
{
    // RootSignatureとPSOを設定し、プリミティブトポロジを三角形リストに指定
    dxCommonRaw_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureSprite());
    dxCommonRaw_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateSprite());
    dxCommonRaw_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

}
