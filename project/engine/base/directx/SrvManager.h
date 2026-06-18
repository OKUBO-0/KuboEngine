#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <vector>

namespace DirectX {
struct TexMetadata;
}

/// @brief SRVディスクリプタヒープの確保と設定を担当するクラス
/// @details テクスチャや StructuredBuffer 用の SRV を生成し、描画前にヒープをバインドする。
namespace Engine::Base {

class DirectXCommon;

class SrvManager
{
public:
	struct UsageRecord {
		uint32_t index = 0;
		std::string usage;
	};
	struct RetiredDescriptor {
		uint32_t index = 0;
		uint64_t fenceValue = 0;
	};

	/// @brief SRV管理を初期化する
	/// @param dxCommon DirectX共通管理
	/// @return なし
	void Initialize(DirectXCommon* dxCommon);

	//アロケータ（ヒープのアドレスを指定するやつ）
	uint32_t Allocate();
	bool Free(uint32_t srvIndex);

	//cpu、gpuの計算用関数
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

	//SRV生成（テクスチャ用）
	void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels, const DirectX::TexMetadata& metadata);
	//SRV生成(structured Buffer用)
	void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements,
		UINT structureByteStride, UINT64 firstElement = 0);
	void PreDraw();
	void SetGraphicsRootDescriptorTable(UINT rootParameterIndex, uint32_t srvIndex);

	bool CheckTexturesNumber();
	bool IsAllocated(uint32_t srvIndex) const;
	uint32_t GetUsedCount() const { return activeCount_; }
	uint32_t GetHighWatermark() const { return useIndex; }
	uint32_t GetMaxCount() const { return kMaxSRVCount; }
	uint32_t GetRemainingCount();
	const std::vector<UsageRecord>& GetUsageRecords() const { return usageRecords_; }
private:
	void SetUsage(uint32_t srvIndex, const std::string& usage);
	void ReclaimCompletedDescriptors();
	void ValidateIndex(uint32_t srvIndex) const;
	void ValidateAllocated(uint32_t srvIndex) const;

	DirectXCommon* directXCommon = nullptr;
	//最大SRV数（最大テクスチャ枚数）
	static const uint32_t kMaxSRVCount;
	//SRV用のデスクリプタサイズ
	uint32_t descriptorSize;
	//SRV用のデスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	//次に使用するSRVインデックス
	uint32_t useIndex = 0;
	uint32_t activeCount_ = 0;
	std::vector<uint32_t> freeIndices_;
	std::vector<RetiredDescriptor> retiredDescriptors_;
	std::vector<uint8_t> allocated_;
	std::vector<UsageRecord> usageRecords_;
	

};

}

