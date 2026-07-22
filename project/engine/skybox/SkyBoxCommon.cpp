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
	srvManager_ = srvManager;

	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon);
	graphicsPipeline_->CreateSkybox();

}

void SkyBoxCommon::Finalize()
{

	graphicsPipeline_.reset();
	dxCommon_.reset();
	srvManager_ = nullptr;

}

void SkyBoxCommon::commonDraw()
{
	const auto dxCommon = dxCommon_.lock();
	if (!dxCommon) {
		return;
	}

	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	const auto rootSignature = graphicsPipeline_->GetRootSignatureSkyboxHandle();
	const auto pipelineState = graphicsPipeline_->GetGraphicsPipelineStateSkyboxHandle();
	dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
	dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
	dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

}
