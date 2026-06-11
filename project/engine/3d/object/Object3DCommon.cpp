#include "Object3DCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "Logger.h"
#include "MyMath.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include <algorithm>
#include <cassert>

namespace {

Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target)
{
	const Vector3 forward = MyMath::Normalize(target - eye);
	Vector3 right = Vector3{ 0.0f, 1.0f, 0.0f }.Cross(forward);
	if (right.Length() < 0.001f) {
		right = Vector3{ 0.0f, 0.0f, 1.0f }.Cross(forward);
	}
	right = MyMath::Normalize(right);
	const Vector3 up = forward.Cross(right);
	return {
		right.x, up.x, forward.x, 0.0f,
		right.y, up.y, forward.y, 0.0f,
		right.z, up.z, forward.z, 0.0f,
		-right.Dot(eye), -up.Dot(eye), -forward.Dot(eye), 1.0f,
	};
}

}

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

	shadowGraphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	shadowGraphicsPipeline_->Initialize(dxCommon_);
	shadowGraphicsPipeline_->CreateShadowMap();

	sceneLightResource_ = dxCommon_->CreateBufferResource(sizeof(SceneLightData));
	sceneLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&sceneLightData_));
	sceneLightData_->color = { 1.0f, 0.95f, 0.9f, 1.0f };
	sceneLightData_->direction = MyMath::Normalize(Vector3{ -0.55f, -1.0f, -0.45f });
	sceneLightData_->intensity = 0.85f;
	sceneLightData_->ambientColor = { 0.48f, 0.52f, 0.62f, 1.0f };
	sceneLightData_->ambientIntensity = 0.34f;
	sceneLightData_->specularStrength = 0.16f;
	sceneLightData_->enable = 1;
	sceneLightData_->padding = 0.0f;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = kShadowMapSize;
	resourceDesc.Height = kShadowMapSize;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	const HRESULT resourceResult = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(shadowMapResource_.GetAddressOf()));
	assert(SUCCEEDED(resourceResult));

	shadowDsvHeap_ = dxCommon_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dxCommon_->GetDevice()->CreateDepthStencilView(
		shadowMapResource_.Get(), &dsvDesc,
		shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());

	shadowSrvIndex_ = srvManager_->Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	dxCommon_->GetDevice()->CreateShaderResourceView(
		shadowMapResource_.Get(), &srvDesc,
		srvManager_->GetCPUDescriptorHandle(shadowSrvIndex_));

	shadowMapDataResource_ = dxCommon_->CreateBufferResource(sizeof(ShadowMapData));
	shadowMapDataResource_->Map(0, nullptr, reinterpret_cast<void**>(&shadowMapData_));
	shadowMapData_->lightViewProjection = MyMath::MakeIdentity4x4();
	shadowMapData_->settings = {
		1.0f,
		1.5f / static_cast<float>(kShadowMapSize),
		0.68f,
		0.0012f,
	};

}

void Object3DCommon::Finalize()
{
	graphicsPipeline_.reset();
	skinningGraphicsPipeline_.reset();
	shadowGraphicsPipeline_.reset();
	sceneLightResource_.Reset();
	sceneLightData_ = nullptr;
	shadowMapResource_.Reset();
	shadowDsvHeap_.Reset();
	shadowMapDataResource_.Reset();
	shadowMapData_ = nullptr;
	dxCommon_ = nullptr;
	srvManager_ = nullptr;
}

void Object3DCommon::BeginShadowPass(const Vector3& focusPosition)
{
	if (!sceneLightData_ || !shadowMapData_) {
		return;
	}
	const Vector3 direction = MyMath::Normalize(sceneLightData_->direction);
	const Vector3 eye = focusPosition - direction * 95.0f;
	const Matrix4x4 view = MakeLookAtMatrix(eye, focusPosition);
	const Matrix4x4 projection = MyMath::MakeOrthographicMatrix(
		-shadowArea_, shadowArea_, shadowArea_, -shadowArea_, 0.1f, 210.0f);
	shadowMapData_->lightViewProjection = view * projection;
	shadowMapData_->settings.x =
		sceneLightData_->enable != 0 && shadowEnabled_ ? 1.0f : 0.0f;

	if (shadowMapState_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
		dxCommon_->TransitionResource(
			shadowMapResource_.Get(), shadowMapState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		shadowMapState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
		shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
	dxCommon_->GetCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);
	dxCommon_->GetCommandList()->ClearDepthStencilView(
		dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kShadowMapSize);
	viewport.Height = static_cast<float>(kShadowMapSize);
	viewport.MaxDepth = 1.0f;
	const D3D12_RECT scissor{
		0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)
	};
	dxCommon_->GetCommandList()->RSSetViewports(1, &viewport);
	dxCommon_->GetCommandList()->RSSetScissorRects(1, &scissor);
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(
		shadowGraphicsPipeline_->GetRootSignatureShadowMap());
	dxCommon_->GetCommandList()->SetPipelineState(
		shadowGraphicsPipeline_->GetGraphicsPipelineStateShadowMap());
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
		1, shadowMapDataResource_->GetGPUVirtualAddress());
	shadowPassActive_ = true;
}

void Object3DCommon::EndShadowPass()
{
	shadowPassActive_ = false;
	if (shadowMapState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		dxCommon_->TransitionResource(
			shadowMapResource_.Get(), shadowMapState_,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		shadowMapState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
	if (Engine::Base::OffscreenRenderManager* offscreen =
			Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->BindRenderTarget();
	}
}

void Object3DCommon::BindSceneLighting(bool skinning)
{
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
		3, sceneLightResource_->GetGPUVirtualAddress());
	const UINT shadowTextureRoot = skinning ? 8u : 7u;
	const UINT shadowDataRoot = skinning ? 9u : 8u;
	srvManager_->SetGraphicsRootDescriptorTable(shadowTextureRoot, shadowSrvIndex_);
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
		shadowDataRoot, shadowMapDataResource_->GetGPUVirtualAddress());
}

void Object3DCommon::SetSceneLight(const SceneLightData& light)
{
	*sceneLightData_ = light;
	if (sceneLightData_->direction.Length() < 0.001f) {
		sceneLightData_->direction = { -0.55f, -1.0f, -0.45f };
	}
	sceneLightData_->direction = MyMath::Normalize(sceneLightData_->direction);
	sceneLightData_->ambientIntensity =
		std::clamp(sceneLightData_->ambientIntensity, 0.0f, 1.0f);
	sceneLightData_->specularStrength =
		std::clamp(sceneLightData_->specularStrength, 0.0f, 1.0f);
}

void Object3DCommon::SetShadowEnabled(bool enabled)
{
	shadowEnabled_ = enabled;
	shadowMapData_->settings.x = enabled ? 1.0f : 0.0f;
}

bool Object3DCommon::IsShadowEnabled() const
{
	return shadowEnabled_;
}

void Object3DCommon::SetShadowStrength(float strength)
{
	shadowMapData_->settings.z = std::clamp(strength, 0.0f, 1.0f);
}

float Object3DCommon::GetShadowStrength() const
{
	return shadowMapData_ ? shadowMapData_->settings.z : 0.0f;
}

void Object3DCommon::SetShadowSoftness(float texels)
{
	shadowMapData_->settings.y =
		std::clamp(texels, 0.5f, 4.0f) / static_cast<float>(kShadowMapSize);
}

float Object3DCommon::GetShadowSoftness() const
{
	return shadowMapData_ ? shadowMapData_->settings.y * kShadowMapSize : 0.0f;
}

void Object3DCommon::SetShadowBias(float bias)
{
	shadowMapData_->settings.w = std::clamp(bias, 0.0f, 0.01f);
}

float Object3DCommon::GetShadowBias() const
{
	return shadowMapData_ ? shadowMapData_->settings.w : 0.0f;
}

void Object3DCommon::SetShadowArea(float area)
{
	shadowArea_ = std::clamp(area, 20.0f, 160.0f);
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


