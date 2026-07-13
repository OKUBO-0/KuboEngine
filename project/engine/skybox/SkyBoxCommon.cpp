#include "SkyBoxCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"

namespace Engine::Skybox {

SkyBoxCommon::~SkyBoxCommon() = default;

SkyBoxCommon* SkyBoxCommon::GetInstance()
{
	static SkyBoxCommon instance;
	return &instance;
}

void SkyBoxCommon::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager) {

	dxCommon_ = dxCommon;
	dxCommonRaw_ = dxCommon.get();
	srvManager_ = srvManager;

	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommonRaw_);
	graphicsPipeline_->CreateSkybox();

}

void SkyBoxCommon::Finalize()
{

	graphicsPipeline_.reset();
	dxCommon_.reset();
	dxCommonRaw_ = nullptr;
	srvManager_ = nullptr;

}

void SkyBoxCommon::commonDraw()
{

	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	dxCommonRaw_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureSkybox());
	dxCommonRaw_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateSkybox());
	dxCommonRaw_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

}
