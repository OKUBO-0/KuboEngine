#include "Object3DCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "HResult.h"
#include "Logger.h"
#include "MyMath.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

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

std::shared_ptr<Engine::Base::DirectXCommon> Object3DCommon::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

void Object3DCommon::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager)
{

	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	//パイプラインの生成
	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon);
	graphicsPipeline_->Create();
	
	skinningGraphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	skinningGraphicsPipeline_->Initialize(dxCommon);
	skinningGraphicsPipeline_->CreateSkinning();

	shadowGraphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	shadowGraphicsPipeline_->Initialize(dxCommon);
	shadowGraphicsPipeline_->CreateShadowMap();

	sceneLightData_.color = { 1.0f, 0.95f, 0.9f, 1.0f };
	sceneLightData_.direction = MyMath::Normalize(Vector3{ -0.55f, -1.0f, -0.45f });
	sceneLightData_.intensity = 0.85f;
	sceneLightData_.ambientColor = { 0.48f, 0.52f, 0.62f, 1.0f };
	sceneLightData_.ambientIntensity = 0.34f;
	sceneLightData_.specularStrength = 0.16f;
	sceneLightData_.enable = 1;
	sceneLightData_.padding = 0.0f;

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
	const HRESULT resourceResult = dxCommon->GetDevice()->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(shadowMapResource_.GetAddressOf()));
	Engine::Base::ThrowIfFailed(
		resourceResult,
		"ID3D12Device::CreateCommittedResource shadow map");
	dxCommon->TrackResourceState(
		shadowMapResource_.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	shadowDsvHeap_ = dxCommon->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dxCommon->GetDevice()->CreateDepthStencilView(
		shadowMapResource_.Get(), &dsvDesc,
		shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());

	shadowSrvIndex_ = srvManager_->Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	dxCommon->GetDevice()->CreateShaderResourceView(
		shadowMapResource_.Get(), &srvDesc,
		srvManager_->GetCPUDescriptorHandle(shadowSrvIndex_));
	srvManager_->LabelUsage(shadowSrvIndex_, "ShadowMap");

	shadowMapData_.lightViewProjection = MyMath::MakeIdentity4x4();
	shadowMapData_.settings = {
		1.0f,
		1.5f / static_cast<float>(kShadowMapSize),
		0.68f,
		0.0012f,
	};

}

void Object3DCommon::Finalize()
{
	if (srvManager_ && shadowSrvIndex_ != UINT32_MAX) {
		srvManager_->Free(shadowSrvIndex_);
		shadowSrvIndex_ = UINT32_MAX;
	}
	graphicsPipeline_.reset();
	skinningGraphicsPipeline_.reset();
	shadowGraphicsPipeline_.reset();
	if (const auto dxCommon = dxCommon_.lock(); dxCommon && shadowMapResource_) {
		dxCommon->UntrackResourceState(shadowMapResource_.Get());
	}
	shadowMapResource_.Reset();
	shadowDsvHeap_.Reset();
	dxCommon_.reset();
	srvManager_ = nullptr;
}

bool Object3DCommon::BeginShadowPass(const Vector3& focusPosition)
{
	const auto dxCommon = GetDirectXCommon();
	shadowPassActive_ = false;
	shadowPassStats_ = {};
	if (!shadowEnabled_ || sceneLightData_.enable == 0) {
		shadowMapData_.settings.x = 0.0f;
		return false;
	}
	dxCommon->BeginShadowGpuTiming();
	const Vector3 direction = MyMath::Normalize(sceneLightData_.direction);
	const Vector3 eye = focusPosition - direction * 95.0f;
	const Matrix4x4 view = MakeLookAtMatrix(eye, focusPosition);
	const Matrix4x4 projection = MyMath::MakeOrthographicMatrix(
		-shadowArea_, shadowArea_, shadowArea_, -shadowArea_, 0.1f, 210.0f);
	shadowMapData_.lightViewProjection = view * projection;
	shadowMapData_.settings.x =
		sceneLightData_.enable != 0 && shadowEnabled_ ? 1.0f : 0.0f;

	dxCommon->TransitionResource(
		shadowMapResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
		shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
	dxCommon->GetCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);
	dxCommon->GetCommandList()->ClearDepthStencilView(
		dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kShadowMapSize);
	viewport.Height = static_cast<float>(kShadowMapSize);
	viewport.MaxDepth = 1.0f;
	const D3D12_RECT scissor{
		0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)
	};
	dxCommon->GetCommandList()->RSSetViewports(1, &viewport);
	dxCommon->GetCommandList()->RSSetScissorRects(1, &scissor);
	const auto rootSignature = shadowGraphicsPipeline_->GetRootSignatureShadowMapHandle();
	const auto pipelineState = shadowGraphicsPipeline_->GetGraphicsPipelineStateShadowMapHandle();
	dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
	dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
	dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const Engine::Base::DirectXCommon::FrameUploadAllocation shadowAllocation =
		dxCommon->AllocateFrameUpload(sizeof(ShadowMapData), 256);
	std::memcpy(
		shadowAllocation.cpuAddress,
		&shadowMapData_,
		sizeof(shadowMapData_));
	dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(
		1, shadowAllocation.gpuAddress);
	shadowPassActive_ = true;
	return true;
}

bool Object3DCommon::IsInsideShadowFrustum(
	const Vector3& worldCenter,
	float boundingRadius) const
{
	if (!shadowPassActive_) {
		return false;
	}
	const float radius = std::isfinite(boundingRadius)
		? (std::max)(0.0f, boundingRadius)
		: 0.0f;
	const Vector3 clipCenter =
		MyMath::Transform(worldCenter, shadowMapData_.lightViewProjection);
	const float horizontalMargin = radius / (std::max)(shadowArea_, 1.0f);
	constexpr float kShadowDepthRange = 210.0f - 0.1f;
	const float depthMargin = radius / kShadowDepthRange;
	return clipCenter.x >= -1.0f - horizontalMargin &&
		clipCenter.x <= 1.0f + horizontalMargin &&
		clipCenter.y >= -1.0f - horizontalMargin &&
		clipCenter.y <= 1.0f + horizontalMargin &&
		clipCenter.z >= -depthMargin &&
		clipCenter.z <= 1.0f + depthMargin;
}

void Object3DCommon::RecordShadowCandidate(bool submitted)
{
	++shadowPassStats_.candidateCount;
	if (submitted) {
		++shadowPassStats_.submittedCount;
	} else {
		++shadowPassStats_.culledCount;
	}
}

void Object3DCommon::EndShadowPass()
{
	const auto dxCommon = GetDirectXCommon();
	shadowPassActive_ = false;
	dxCommon->TransitionResource(
		shadowMapResource_.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (Engine::Base::OffscreenRenderManager* offscreen =
			Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->BindRenderTarget();
	}
	dxCommon->EndShadowGpuTiming();
	++measuredShadowPassCount_;
	totalShadowCandidateCount_ += shadowPassStats_.candidateCount;
	totalShadowSubmittedCount_ += shadowPassStats_.submittedCount;
	totalShadowCulledCount_ += shadowPassStats_.culledCount;
}

void Object3DCommon::ResetShadowPassStatistics()
{
	measuredShadowPassCount_ = 0;
	totalShadowCandidateCount_ = 0;
	totalShadowSubmittedCount_ = 0;
	totalShadowCulledCount_ = 0;
}

void Object3DCommon::BindSceneLighting(bool skinning)
{
	const auto dxCommon = GetDirectXCommon();
	const Engine::Base::DirectXCommon::FrameUploadAllocation lightAllocation =
		dxCommon->AllocateFrameUpload(sizeof(SceneLightData), 256);
	std::memcpy(
		lightAllocation.cpuAddress,
		&sceneLightData_,
		sizeof(sceneLightData_));
	const Engine::Base::DirectXCommon::FrameUploadAllocation shadowAllocation =
		dxCommon->AllocateFrameUpload(sizeof(ShadowMapData), 256);
	std::memcpy(
		shadowAllocation.cpuAddress,
		&shadowMapData_,
		sizeof(shadowMapData_));

	dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(
		3, lightAllocation.gpuAddress);
	const UINT shadowTextureRoot = skinning ? 8u : 7u;
	const UINT shadowDataRoot = skinning ? 9u : 8u;
	srvManager_->SetGraphicsRootDescriptorTable(shadowTextureRoot, shadowSrvIndex_);
	dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(
		shadowDataRoot, shadowAllocation.gpuAddress);
}

void Object3DCommon::SetSceneLight(const SceneLightData& light)
{
	sceneLightData_ = light;
	if (sceneLightData_.direction.Length() < 0.001f) {
		sceneLightData_.direction = { -0.55f, -1.0f, -0.45f };
	}
	sceneLightData_.direction = MyMath::Normalize(sceneLightData_.direction);
	sceneLightData_.ambientIntensity =
		std::clamp(sceneLightData_.ambientIntensity, 0.0f, 1.0f);
	sceneLightData_.specularStrength =
		std::clamp(sceneLightData_.specularStrength, 0.0f, 1.0f);
}

void Object3DCommon::SetShadowEnabled(bool enabled)
{
	shadowEnabled_ = enabled;
	shadowMapData_.settings.x = enabled ? 1.0f : 0.0f;
}

bool Object3DCommon::IsShadowEnabled() const
{
	return shadowEnabled_;
}

void Object3DCommon::SetShadowStrength(float strength)
{
	shadowMapData_.settings.z = std::clamp(strength, 0.0f, 1.0f);
}

float Object3DCommon::GetShadowStrength() const
{
	return shadowMapData_.settings.z;
}

void Object3DCommon::SetShadowSoftness(float texels)
{
	shadowMapData_.settings.y =
		std::clamp(texels, 0.5f, 4.0f) / static_cast<float>(kShadowMapSize);
}

float Object3DCommon::GetShadowSoftness() const
{
	return shadowMapData_.settings.y * kShadowMapSize;
}

void Object3DCommon::SetShadowBias(float bias)
{
	shadowMapData_.settings.w = std::clamp(bias, 0.0f, 0.01f);
}

float Object3DCommon::GetShadowBias() const
{
	return shadowMapData_.settings.w;
}

void Object3DCommon::SetShadowArea(float area)
{
	shadowArea_ = std::clamp(area, 20.0f, 160.0f);
}

void Object3DCommon::CommonDraw()
{
	const auto dxCommon = GetDirectXCommon();

	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	const auto rootSignature = graphicsPipeline_->GetRootSignatureObjectHandle();
	const auto pipelineState = graphicsPipeline_->GetGraphicsPipelineStateObjectHandle();
	dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
	dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
	dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

void Object3DCommon::SkinningCommonDraw()
{
	const auto dxCommon = GetDirectXCommon();
	//RootSignatureを設定。POSに設定しているけどベット設定が必要
	const auto rootSignature = skinningGraphicsPipeline_->GetRootSignatureSkinningHandle();
	const auto pipelineState = skinningGraphicsPipeline_->GetGraphicsPipelineStateSkinningHandle();
	dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
	dxCommon->GetCommandList()->SetPipelineState(pipelineState.Get());
	dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


}

}


