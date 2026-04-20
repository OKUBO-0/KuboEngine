#include "SkyBoxCommon.h"

namespace Engine::Skybox {

SkyBoxCommon* SkyBoxCommon::GetInstance()
{
	static SkyBoxCommon instance;
	return &instance;
}

void SkyBoxCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvmanager) {

	dxCommon_ = dxCommon;
	srvManager_ = srvmanager;

	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon_);
	graphicsPipeline_->CreateSkybox();

}

void SkyBoxCommon::Finalize()
{

	graphicsPipeline_.reset();
	dxCommon_ = nullptr;
	srvManager_ = nullptr;

}

void SkyBoxCommon::commonDraw()
{

	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureSkybox());
	dxCommon_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateSkybox());
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

}
