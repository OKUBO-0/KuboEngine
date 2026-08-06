#include "SrvManager.h"
#include "DirectXCommon.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>

namespace Engine::Base {

const uint32_t SrvManager::kMaxSRVCount = 2048;
void SrvManager::Initialize(std::shared_ptr<DirectXCommon> dxCommon)
{
	assert(dxCommon);
	directXCommon_ = dxCommon;
	//デスクリプタヒープの生成
	descriptorHeap = dxCommon->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);
	//デスクリプタ1個分のサイズを取得して記録
	descriptorSize = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	useIndex = 0;
	activeCount_ = 0;
	freeIndices_.clear();
	freeIndices_.reserve(kMaxSRVCount);
	retiredDescriptors_.clear();
	retiredDescriptors_.reserve(kMaxSRVCount);
	allocated_.assign(kMaxSRVCount, uint8_t{ 0 });
	usageRecords_.clear();
	usageRecords_.reserve(kMaxSRVCount);

}


uint32_t SrvManager::Allocate()
{
	ReclaimCompletedDescriptors();
	if (!CheckTexturesNumber()) {
		throw std::runtime_error("SrvManager descriptor heap exhausted");
	}

	uint32_t index = 0;
	if (!freeIndices_.empty()) {
		index = freeIndices_.back();
		freeIndices_.pop_back();
	} else {
		index = useIndex;
		++useIndex;
	}
	allocated_[index] = 1;
	++activeCount_;
	SetUsage(index, "Allocated");
	return index;
}

bool SrvManager::Free(uint32_t srvIndex)
{
	if (srvIndex >= kMaxSRVCount || !allocated_[srvIndex]) {
		assert(false && "SrvManager::Free received an invalid or already freed index");
		return false;
	}

	allocated_[srvIndex] = 0;
	--activeCount_;
	const uint64_t fenceValue = GetDirectXCommon()->GetPendingSubmissionFenceValue();
	retiredDescriptors_.push_back({ srvIndex, fenceValue });
	SetUsage(srvIndex, "Retired");
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index)
{
	ValidateAllocated(index);
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index)
{
	ValidateAllocated(index);
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

void SrvManager::CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels, const DirectX::TexMetadata& metadata)
{
	ValidateAllocated(srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{  };
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (metadata.IsCubemap()) {

		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;//キューブマップテクスチャ
		srvDesc.TextureCube.MipLevels = UINT_MAX;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	} else {

		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
		srvDesc.Texture2D.MipLevels = UINT(MipLevels);

	}


	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
	SetUsage(srvIndex, metadata.IsCubemap() ? "TextureCube" : "Texture2D");

}

void SrvManager::CreateSRVforStructuredBuffer(
	uint32_t srvIndex,
	ID3D12Resource* pResource,
	UINT numElements,
	UINT structureByteStride,
	UINT64 firstElement)
{
	ValidateAllocated(srvIndex);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = firstElement;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = numElements;
	srvDesc.Buffer.StructureByteStride = structureByteStride;

	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
	SetUsage(srvIndex,
		"StructuredBuffer elements=" + std::to_string(numElements) +
		" stride=" + std::to_string(structureByteStride));

}

void SrvManager::PreDraw()
{
	//描画用のDescriptorHeapの設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorHeap.Get() };
	GetDirectXCommon()->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);

}

void SrvManager::SetGraphicsRootDescriptorTable(UINT rootParameterIndex, uint32_t srvIndex)
{
	ValidateAllocated(srvIndex);
	GetDirectXCommon()->GetCommandList()->SetGraphicsRootDescriptorTable(rootParameterIndex, GetGPUDescriptorHandle(srvIndex));
}

bool SrvManager::CheckTexturesNumber()
{
	ReclaimCompletedDescriptors();
	return !freeIndices_.empty() || useIndex < kMaxSRVCount;
}

uint32_t SrvManager::GetRemainingCount()
{
	ReclaimCompletedDescriptors();
	return static_cast<uint32_t>(freeIndices_.size()) + (kMaxSRVCount - useIndex);
}

void SrvManager::ReclaimCompletedDescriptors()
{
	const uint64_t completedFenceValue = GetDirectXCommon()->GetCompletedFenceValue();
	auto firstPending = std::remove_if(
		retiredDescriptors_.begin(),
		retiredDescriptors_.end(),
		[this, completedFenceValue](const RetiredDescriptor& retired) {
			if (retired.fenceValue > completedFenceValue) {
				return false;
			}
			freeIndices_.push_back(retired.index);
			SetUsage(retired.index, "Free");
			return true;
		});
	retiredDescriptors_.erase(firstPending, retiredDescriptors_.end());
}

bool SrvManager::IsAllocated(uint32_t srvIndex) const
{
	return srvIndex < allocated_.size() && allocated_[srvIndex] != 0;
}

std::shared_ptr<DirectXCommon> SrvManager::GetDirectXCommon() const
{
	auto dxCommon = directXCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

SrvManager::UsageSummary SrvManager::GetUsageSummary() const
{
	UsageSummary summary;
	for (const UsageRecord& record : usageRecords_) {
		if (!IsAllocated(record.index)) {
			continue;
		}
		if (record.usage == "Texture2D") {
			++summary.texture2D;
		} else if (record.usage == "TextureCube") {
			++summary.textureCube;
		} else if (record.usage.starts_with("StructuredBuffer") ||
			record.usage == "LineInstanceBuffer" ||
			record.usage == "ParticleInstanceBuffer" ||
			record.usage == "SkinPalette" ||
			record.usage == "ObjectInstanceData") {
			++summary.structuredBuffer;
		} else if (record.usage == "ShadowMap") {
			++summary.shadowMap;
		} else {
			++summary.other;
		}
	}
	return summary;
}

void SrvManager::LabelUsage(uint32_t srvIndex, const std::string& usage)
{
	ValidateAllocated(srvIndex);
	SetUsage(srvIndex, usage);
}

void SrvManager::ValidateIndex(uint32_t srvIndex) const
{
	if (srvIndex >= kMaxSRVCount) {
		throw std::out_of_range("SrvManager descriptor index is out of range");
	}
}

void SrvManager::ValidateAllocated(uint32_t srvIndex) const
{
	ValidateIndex(srvIndex);
	if (!allocated_[srvIndex]) {
		throw std::logic_error("SrvManager descriptor index is not allocated");
	}
}

void SrvManager::SetUsage(uint32_t srvIndex, const std::string& usage)
{
	auto it = std::find_if(
		usageRecords_.begin(),
		usageRecords_.end(),
		[srvIndex](const UsageRecord& record) { return record.index == srvIndex; });
	if (it != usageRecords_.end()) {
		it->usage = usage;
		return;
	}
	usageRecords_.push_back({ srvIndex, usage });
}

}
