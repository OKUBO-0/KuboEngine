#include "LineCommon.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "HResult.h"
#include "MyMath.h"
#include "SrvManager.h"
#include <CameraManager.h>

namespace Engine::LineSystem {

LineCommon::~LineCommon() = default;

const Vector3 LineCommon::kDefaultLineStart_{ 0.0f, 0.0f, 0.0f };
const Vector3 LineCommon::kDefaultLineEnd_{ 0.0f, 1.0f, 1.0f };
const Vector4 LineCommon::kDefaultLineColor_{ 1.0f, 0.0f, 0.0f, 1.0f };
const std::array<VertexDataLine, 2> LineCommon::kDefaultLineVertices_{ {
	{{0.0f, 0.0f, 0.0f}},
	{{1.0f, 0.0f, 0.0f}},
} };

LineCommon* LineCommon::GetInstance()
{
	static LineCommon instance;
	return &instance;
}

void LineCommon::InitializePipeline()
{
	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon_);
	graphicsPipeline_->CreateLine();
}

void LineCommon::InitializeVertexResources()
{
	const size_t vertexBytes = sizeof(VertexDataLine) * linevertices.size();
	vertexResource_ = dxCommon_->CreateDefaultBufferResource(
		linevertices.data(),
		vertexBytes,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBytes);
	vertexBufferView_.StrideInBytes = sizeof(VertexDataLine);
}

void LineCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	InitializePipeline();
	InitializeVertexResources();
	for (uint32_t& srvIndex : instanceSrvIndices_) {
		srvIndex = srvManager_->Allocate();
		srvManager_->LabelUsage(srvIndex, "LineInstanceBuffer");
	}
}

void LineCommon::UpdateCameraBuffer()
{
	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (activeCamera) {
		cameraData_.projection = activeCamera->GetProjectionMatrix();
		cameraData_.view = activeCamera->GetViewMatrix();
	}

}

void LineCommon::Finalize()
{
	if (srvManager_) {
		for (uint32_t& srvIndex : instanceSrvIndices_) {
			if (srvIndex != UINT32_MAX) {
				srvManager_->Free(srvIndex);
				srvIndex = UINT32_MAX;
			}
		}
	}
	graphicsPipeline_.reset();
	if (dxCommon_) {
		dxCommon_->UntrackResourceState(vertexResource_.Get());
	}
	vertexResource_.Reset();
	instances_.clear();
	dxCommon_ = nullptr;
	srvManager_ = nullptr;
}

void LineCommon::CommonDraw()
{
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureLine());
	dxCommon_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateLine());
	// 1本ずつ独立した線なので LINESTRIP ではなく LINELIST
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
}

void LineCommon::Update()
{
	UpdateCameraBuffer();
}

void LineCommon::Draw()
{
	if (instances_.empty()) return;

	const Engine::Base::DirectXCommon::FrameUploadAllocation cameraAllocation =
		dxCommon_->AllocateFrameUpload(sizeof(CameraBufferforGpu), 256);
	memcpy(cameraAllocation.cpuAddress, &cameraData_, sizeof(cameraData_));
	const size_t instanceSize =
		sizeof(LineInstanceData) * instances_.size();
	const Engine::Base::DirectXCommon::FrameUploadAllocation instanceAllocation =
		dxCommon_->AllocateFrameUpload(instanceSize, sizeof(LineInstanceData));
	memcpy(instanceAllocation.cpuAddress, instances_.data(), instanceSize);
	const uint32_t frameIndex = dxCommon_->GetCurrentFrameIndex();
	const uint32_t srvIndex = instanceSrvIndices_[frameIndex];
	srvManager_->CreateSRVforStructuredBuffer(
		srvIndex,
		instanceAllocation.resource,
		static_cast<UINT>(instances_.size()),
		sizeof(LineInstanceData),
		instanceAllocation.offset / sizeof(LineInstanceData));

	CommonDraw();
	dxCommon_->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
	// RootParameter[0] → b0：カメラ（CBV）
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
		0,
		cameraAllocation.gpuAddress);
	srvManager_->SetGraphicsRootDescriptorTable(1, srvIndex);
	dxCommon_->GetCommandList()->DrawInstanced(2, static_cast<UINT>(instances_.size()), 0, 0);

	instances_.clear(); // ← 正しい変数名




}

void LineCommon::DrawLine(const Vector3& start, const Vector3& end, const Vector4& color)
{
	instances_.push_back({ start, end, color });


}

}
