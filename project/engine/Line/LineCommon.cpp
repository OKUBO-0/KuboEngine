#include "LineCommon.h"
#include "MyMath.h"
#include <CameraManager.h>

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
	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexDataLine) * linevertices.size());
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexDataLine) * linevertices.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexDataLine);

	void* mapped = nullptr;
	vertexResource_->Map(0, nullptr, &mapped);
	memcpy(mapped, linevertices.data(), sizeof(VertexDataLine) * linevertices.size());
}

void LineCommon::InitializeCameraResource()
{
	cameraResource = dxCommon_->CreateBufferResource(sizeof(CameraBufferforGpu));
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&camerabuffer));
}

void LineCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	InitializePipeline();
	InitializeVertexResources();
	InitializeCameraResource();
	instanceSrvIndex_ = UINT32_MAX;
}

void LineCommon::UpdateCameraBuffer()
{
	camerabuffer->projection = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->GetProjectionMatrix();
	camerabuffer->view = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->GetViewMatrix();
}

void LineCommon::EnsureInstanceResourceCapacity(size_t instanceSize)
{
	if (instanceResource_ && instanceResource_->GetDesc().Width >= instanceSize) {
		return;
	}

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(instanceSize);
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&instanceResource_));
	assert(SUCCEEDED(hr));
}

void LineCommon::UploadInstances(size_t instanceSize)
{
	LineInstanceData* mapped = nullptr;
	instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	memcpy(mapped, instances_.data(), instanceSize);
	instanceResource_->Unmap(0, nullptr);
}

void LineCommon::EnsureInstanceSrvIndex()
{
	if (instanceSrvIndex_ == UINT32_MAX) {
		instanceSrvIndex_ = srvManager_->Allocate();
	}
}

void LineCommon::UpdateInstanceSrv()
{
	srvManager_->CreateSRVforStructuredBuffer(
		instanceSrvIndex_,
		instanceResource_.Get(),
		static_cast<UINT>(instances_.size()),
		sizeof(LineInstanceData));
}

void LineCommon::Finalize()
{
	graphicsPipeline_.reset();
	instanceResource_.Reset();
	vertexResource_.Reset();
	cameraResource.Reset();
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
	if (instances_.empty()) return;

	size_t instanceSize = sizeof(LineInstanceData) * instances_.size();
	EnsureInstanceResourceCapacity(instanceSize);
	UploadInstances(instanceSize);
	EnsureInstanceSrvIndex();
	UpdateInstanceSrv();
}

void LineCommon::Draw()
{
	if (instances_.empty()) return;

	CommonDraw();
	dxCommon_->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
	// RootParameter[0] → b0：カメラ（CBV）
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(0, cameraResource->GetGPUVirtualAddress());
	srvManager_->SetGraficsRootDescriptorTable(1, instanceSrvIndex_);
	dxCommon_->GetCommandList()->DrawInstanced(2, static_cast<UINT>(instances_.size()), 0, 0);

	instances_.clear(); // ← 正しい変数名




}

void LineCommon::DrawLine(const Vector3& start, const Vector3& end, const Vector4& color)
{
	instances_.push_back({ start, end, color });


}
