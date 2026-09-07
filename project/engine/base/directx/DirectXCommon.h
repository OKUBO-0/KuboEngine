#pragma once
#include "ShaderCompiler.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <array>
#include <cstddef>
#include <dxcapi.h>
#include <memory>
#pragma comment(lib, "dxcompiler.lib")
#include"externals/DirectXTex/DirectXTex.h"
#include"externals/DirectXTex/d3dx12.h"
#include<vector>
#include <chrono>
#include <thread>  // std::this_thread
#include <unordered_map>
#include <Vector4.h>

/// @brief DirectX12 のデバイス生成とフレーム描画基盤を管理するクラス
/// @details デバイス、コマンド、スワップチェーン、各種ディスクリプタヒープを初期化し、
///          描画開始と終了の共通処理を提供する。
namespace Engine::Base {

class WinApp;
class DirectXCommon
{
public:
	static constexpr uint32_t kFrameCount = 2;

	struct FrameUploadAllocation
	{
		void* cpuAddress = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		ID3D12Resource* resource = nullptr;
		size_t offset = 0;
		size_t size = 0;
	};

private:
	struct FrameContext
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadArena;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> deferredReleaseResources;
		std::byte* uploadCpuAddress = nullptr;
		size_t uploadOffset = 0;
		uint64_t fenceValue = 0;
		bool gpuTimestampSubmitted = false;
		bool shadowTimestampSubmitted = false;
		bool shadowTimestampRecording = false;
	};

	void DeviceInitialize();
	void EnableDebugLayer();
	Microsoft::WRL::ComPtr<IDXGIAdapter4> SelectAdapter();
	void CreateDevice(IDXGIAdapter4* adapter);
	void ConfigureInfoQueue();
	void CommandInitialize();
	void SwapChainInitialize();
	void DepthBufferInitialize();
	void DescriptorHeapInitialize();
	void RTVInitialize();
	void DSVInitialize();
	void FenceInitialize();
	void ViewportInitialize();
	void ScissorInitialize();
	void ImguiInitialize();
	void InitializeGraphicsResources();
	void ResizeSwapChainIfNeeded();
	void PrepareBackBufferForRendering(uint32_t backBufferIndex);
	void CloseAndExecuteCommandList();
	void FinalizeFrameTransition();
	void WaitForGpuCompletion();
	void SignalFrame(FrameContext& frameContext);
	void WaitForFrame(const FrameContext& frameContext);
	void ResetCommandObjects(uint32_t frameIndex);
	void InitializeFrameUploadArenas();
	void InitializeGpuTiming();
	void CollectCompletedGpuTiming(uint32_t frameIndex);

public:
	~DirectXCommon();

	/// @brief DirectX12 の描画基盤を初期化する
	/// @param winApp ウィンドウ情報を持つアプリケーション管理クラス
	/// @return なし
	void Initialize(Engine::Base::WinApp* winApp);

	/// @brief フレーム描画前の共通設定を行う
	/// @param なし
	/// @return なし
	void Begin();
	void PrepareForFrame();
	void BeginGpuFrameTiming();

	/// @brief フレーム描画後の表示反映と同期を行う
	/// @param なし
	/// @return なし
	void End();

	/// @brief RTV 用ヒープの CPU デスクリプタハンドルを取得する
	/// @param index 取得したい RTV のインデックス
	/// @return 指定インデックスの CPU デスクリプタハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVCPUDescriptorHandle(uint32_t index);

	/// @brief RTV 用ヒープの GPU デスクリプタハンドルを取得する
	/// @param index 取得したい RTV のインデックス
	/// @return 指定インデックスの GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetRTVGPUDescriptorHandle(uint32_t index);

	/// @brief DSV 用ヒープの CPU デスクリプタハンドルを取得する
	/// @param index 取得したい DSV のインデックス
	/// @return 指定インデックスの CPU デスクリプタハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVCPUDescriptorHandle(uint32_t index);

	/// @brief DSV 用ヒープの GPU デスクリプタハンドルを取得する
	/// @param index 取得したい DSV のインデックス
	/// @return 指定インデックスの GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetDSVGPUDescriptorHandle(uint32_t index);

	/// @brief 任意のディスクリプタヒープから CPU ハンドルを計算する
	/// @param descriptorHeap 対象のディスクリプタヒープ
	/// @param descriptorSize ディスクリプタサイズ
	/// @param index 取得したいインデックス
	/// @return 計算後の CPU デスクリプタハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap,
		uint32_t descriptorSize, uint32_t index);

	/// @brief 任意のディスクリプタヒープから GPU ハンドルを計算する
	/// @param descriptorHeap 対象のディスクリプタヒープ
	/// @param descriptorSize ディスクリプタサイズ
	/// @param index 取得したいインデックス
	/// @return 計算後の GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap,
		uint32_t descriptorSize, uint32_t index);

	/// @brief DirectX デバイスを取得する
	/// @param なし
	/// @return D3D12 デバイス
	ID3D12Device* GetDevice() const { return device.Get(); }

	/// @brief コマンドリストを取得する
	/// @param なし
	/// @return 描画コマンドリスト
	ID3D12GraphicsCommandList* GetCommandList()const { return commandList.Get(); }

	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
	uint32_t GetCurrentFrameIndex() const { return currentFrameIndex_; }
	uint64_t GetPendingSubmissionFenceValue() const { return nextFenceValue_ + 1; }
	uint64_t GetCompletedFenceValue() const { return fence ? fence->GetCompletedValue() : 0; }
	void WaitForAllFrames();
	void BeginShadowGpuTiming();
	void EndShadowGpuTiming();
	void ResetGpuTimingStatistics();
	double GetAverageFrameGpuMilliseconds() const;
	double GetAverageShadowGpuMilliseconds() const;
	double GetFrameGpuP95Milliseconds() const;
	double GetShadowGpuP95Milliseconds() const;
	double GetFrameGpuMaxMilliseconds() const;
	double GetShadowGpuMaxMilliseconds() const;
	uint64_t GetFrameGpuSampleCount() const { return frameGpuSampleCount_; }
	uint64_t GetShadowGpuSampleCount() const { return shadowGpuSampleCount_; }

	/// @brief RTV のビュー記述子を取得する
	/// @param なし
	/// @return レンダーターゲットビュー記述子
	const D3D12_RENDER_TARGET_VIEW_DESC& GetRTVDesc() const { return rtvDesc; }

	/// @brief DSV ディスクリプタヒープを取得する
	/// @param なし
	/// @return DSV ディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetDSVDescriptorHeap() { return dsvDescriptorHeap; }

	/// @brief 現在のビューポート設定を取得する
	/// @param なし
	/// @return ビューポート情報
	D3D12_VIEWPORT GetViewport() const { return viewport; }

	/// @brief 現在のシザー矩形を取得する
	/// @param なし
	/// @return シザー矩形
	D3D12_RECT GetScissorRect() const { return scissorRect; }

	/// @brief シェーダーファイルをコンパイルする
	/// @param filePath コンパイル対象のシェーダーファイルパス
	/// @param profile 使用するシェーダープロファイル
	/// @return コンパイル済みシェーダーバイトコード
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile);

	/// @brief ディスクリプタヒープを生成する
	/// @param heaptype 生成するヒープ種別
	/// @param numDescriptrs 確保するディスクリプタ数
	/// @param shaderVisible シェーダー参照可能にするか
	/// @return 生成したディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heaptype,
		UINT numDescriptrs, bool shaderVisible);

	/// @brief アップロード用バッファリソースを生成する
	/// @param sizeInBytes バッファサイズ
	/// @return 生成したバッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBufferResource(
		const void* data,
		size_t sizeInBytes,
		D3D12_RESOURCE_STATES finalState);

	/// @brief 現在のframeが所有するupload arenaから一時領域を確保する
	/// @param sizeInBytes 必要なバイト数
	/// @param alignment CPU/GPU addressのアラインメント
	/// @return CPU書込先とGPU仮想アドレス
	FrameUploadAllocation AllocateFrameUpload(
		size_t sizeInBytes,
		size_t alignment = 16);
	size_t GetCurrentFrameUploadUsedBytes() const;
	size_t GetFrameUploadArenaSizeBytes() const { return kFrameUploadArenaSize; }

	/// @brief 現在のframeが完了するまで一時リソースを保持する
	/// @param resource GPU参照完了後に解放するリソース
	void DeferResourceRelease(Microsoft::WRL::ComPtr<ID3D12Resource>&& resource);

	/// @brief テクスチャメタデータに基づいてリソースを生成する
	/// @param metadata テクスチャのメタデータ
	/// @return 生成したテクスチャリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// @brief テクスチャデータを GPU リソースへ転送する
	/// @param texture 転送先のテクスチャリソース
	/// @param mipImages 転送するミップマップ画像群
	/// @return 転送完了後の中間リソース
	[[nodiscard]]
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	/// @brief バックバッファ数を取得する
	/// @param なし
	/// @return バックバッファ数
	size_t GetBackBufferCount()const { return swapChainResources.size(); }

	/// @brief コマンドリストを実行キューへ送る
	/// @param なし
	/// @return なし
	void CommandKick();

	bool IsFrameRateLimitEnabled() const { return frameRateLimitEnabled_; }
	void SetFrameRateLimitEnabled(bool enabled);
	float GetTargetFrameRate() const { return targetFrameRate_; }
	void SetTargetFrameRate(float frameRate);
	bool IsVSyncEnabled() const { return vSyncEnabled_; }
	void SetVSyncEnabled(bool enabled) { vSyncEnabled_ = enabled; }

	/// @brief リソースの状態遷移バリアを発行する
	/// @param resource 遷移対象リソース
	/// @param before 遷移前の状態
	/// @param after 遷移後の状態
	/// @return なし
	void TrackResourceState(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES initialState,
		UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void UntrackResourceState(ID3D12Resource* resource);
	void TransitionResource(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES after,
		UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void TransitionResource(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after,
		UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertUavBarrier(ID3D12Resource* resource = nullptr);
	void InsertAliasingBarrier(ID3D12Resource* beforeResource,
		ID3D12Resource* afterResource);
	size_t GetTrackedResourceStateCount() const { return resourceStates_.size(); }

private:
	struct ResourceStateKey {
		ID3D12Resource* resource = nullptr;
		UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		bool operator==(const ResourceStateKey&) const = default;
	};
	struct ResourceStateKeyHash {
		size_t operator()(const ResourceStateKey& key) const noexcept
		{
			return std::hash<ID3D12Resource*>{}(key.resource) ^
				(static_cast<size_t>(key.subresource) << 1);
		}
	};
	D3D12_RESOURCE_STATES GetTrackedResourceState(
		ID3D12Resource* resource, UINT subresource) const;
	UINT GetResourceSubresourceCount(ID3D12Resource* resource) const;
	void ExpandWholeResourceState(ID3D12Resource* resource);

	// Windows API 管理
	Engine::Base::WinApp* winApp_ = nullptr;
	HRESULT hr;
	// デバイス
	Microsoft::WRL::ComPtr< IDXGIFactory7> dxgiFactory = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	// コマンド
	static constexpr size_t kFrameUploadArenaSize = 4 * 1024 * 1024;
	std::array<FrameContext, kFrameCount> frameContexts_;
	uint32_t currentFrameIndex_ = 0;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> gpuTimestampQueryHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gpuTimestampReadback_;
	uint64_t* gpuTimestampCpuData_ = nullptr;
	uint64_t gpuTimestampFrequency_ = 0;
	double frameGpuMillisecondsTotal_ = 0.0;
	double shadowGpuMillisecondsTotal_ = 0.0;
	uint64_t frameGpuSampleCount_ = 0;
	uint64_t shadowGpuSampleCount_ = 0;
	std::vector<double> frameGpuMillisecondsSamples_;
	std::vector<double> shadowGpuMillisecondsSamples_;
	// スワップチェーン
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>swapChainResources;
	// 深度バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStenciResource;
	// ディスクリプタヒープ
	uint32_t descriptorSizeRTV;
	uint32_t descriptorSizeDSV;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;


	// RTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle;
	// ダブルバッファ用に 2 つの RTV を保持する
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvHandles;
	// Fence
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	HANDLE fenceEvent = nullptr;
	uint64_t nextFenceValue_ = 0;
	// ビューポート
	D3D12_VIEWPORT viewport{};
	// シザー矩形
	D3D12_RECT scissorRect{};
	std::unique_ptr<ShaderCompiler> shaderCompiler_;
	std::unordered_map<ResourceStateKey, D3D12_RESOURCE_STATES,
		ResourceStateKeyHash> resourceStates_;
	// FPS 固定用の基準時刻
	std::chrono::steady_clock::time_point reference_;
	bool frameRateLimitEnabled_ = true;
	bool vSyncEnabled_ = true;
	float targetFrameRate_ = 60.0f;

	

private:


	// FPS 固定初期化
	void InitializeFixFPS();
	// FPS 固定更新
	void UpdateFixFPS();




};

}

