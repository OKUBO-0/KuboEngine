#include "Object3DCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "Logger.h"

namespace Engine::Graphics3D {

Object3DCommon::~Object3DCommon() = default;

Object3DCommon* Object3DCommon::GetInstance()
{
	static Object3DCommon instance;
	return &instance;

}

void Object3DCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{

	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	//パイプラインの生成
	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon_);
	graphicsPipeline_->Create();
	
	skinningGraphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	skinningGraphicsPipeline_->Initialize(dxCommon_);
	skinningGraphicsPipeline_->CreateSkinning();



}

void Object3DCommon::Finalize()
{
	graphicsPipeline_.reset();
	skinningGraphicsPipeline_.reset();
	dxCommon_ = nullptr;
	srvManager_ = nullptr;
}

void Object3DCommon::CommonDraw()
{

	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
	dxCommon_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineState());
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

void Object3DCommon::SkinningCommonDraw()
{
	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(skinningGraphicsPipeline_->GetRootSignatureSkinning());
	dxCommon_->GetCommandList()->SetPipelineState(skinningGraphicsPipeline_->GetGraphicsPipelineStateSkinning());
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


}

}


