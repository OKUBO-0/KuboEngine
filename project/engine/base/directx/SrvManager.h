#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

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
	/// @brief SRV管理を初期化する
	/// @param dxCommon DirectX共通管理
	/// @return なし
	void Initialize(DirectXCommon* dxCommon);

	//アロケータ（ヒープのアドレスを指定するやつ）
	uint32_t Allocate();

	//cpu、gpuの計算用関数
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

	//SRV生成（テクスチャ用）
	void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels, const DirectX::TexMetadata& metadata);
	//SRV生成(structured Buffer用)
	void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements,
		UINT structureByteStride);
	void PreDraw();
	void SetGraphicsRootDescriptorTable(UINT rootParameterIndex, uint32_t srvIndex);

	bool CheckTexturesNumber();
private:
	DirectXCommon* directXCommon = nullptr;
	//最大SRV数（最大テクスチャ枚数）
	static const uint32_t kMaxSRVCount;
	//SRV用のデスクリプタサイズ
	uint32_t descriptorSize;
	//SRV用のデスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	//次に使用するSRVインデックス
	uint32_t useIndex = 0;
	

};

}

