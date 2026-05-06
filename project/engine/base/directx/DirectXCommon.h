#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <array>
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#include"externals/DirectXTex/DirectXTex.h"
#include"externals/DirectXTex/d3dx12.h"
#include<vector>
#include <chrono>
#include <thread>  // std::this_thread
#include <Vector4.h>

/// @brief DirectX12 のデバイス生成とフレーム描画基盤を管理するクラス
/// @details デバイス、コマンド、スワップチェーン、各種ディスクリプタヒープを初期化し、
///          描画開始と終了の共通処理を提供する。
namespace Engine::Base {

class WinApp;

class DirectXCommon
{
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
	void DxcCompilerInitialize();
	void ImguiInitialize();
	void InitializeGraphicsResources();
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> LoadShaderSource(const std::wstring& filePath);
	DxcBuffer CreateShaderSourceBuffer(IDxcBlobEncoding* shaderSource) const;
	std::array<LPCWSTR, 9> CreateShaderCompileArguments(const std::wstring& filePath, const wchar_t* profile) const;
	Microsoft::WRL::ComPtr<IDxcResult> ExecuteShaderCompile(const DxcBuffer& shaderSourceBuffer,
		std::array<LPCWSTR, 9> arguments);
	void ValidateShaderCompileResult(IDxcResult* shaderResult);
	void PrepareBackBufferForRendering(uint32_t backBufferIndex);
	void CloseAndExecuteCommandList();
	void FinalizeFrameTransition();
	void WaitForGpuCompletion();
	void ResetCommandObjects();

public:
	/// @brief DirectX12 の描画基盤を初期化する
	/// @param winApp ウィンドウ情報を持つアプリケーション管理クラス
	/// @return なし
	void Initialize(Engine::Base::WinApp* winApp);

	/// @brief フレーム描画前の共通設定を行う
	/// @param なし
	/// @return なし
	void Begin();

	/// @brief フレーム描画後の表示反映と同期を行う
	/// @param なし
	/// @return なし
	void End();

	/// @brief SRV 用ヒープの CPU デスクリプタハンドルを取得する
	/// @param index 取得したい SRV のインデックス
	/// @return 指定インデックスの CPU デスクリプタハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

	/// @brief SRV 用ヒープの GPU デスクリプタハンドルを取得する
	/// @param index 取得したい SRV のインデックス
	/// @return 指定インデックスの GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

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
	IDxcBlob* CompileShader(
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

	/// @brief リソースの状態遷移バリアを発行する
	/// @param resource 遷移対象リソース
	/// @param before 遷移前の状態
	/// @param after 遷移後の状態
	/// @return なし
	void TransitionResource(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after);

	
	/// @brief 最大 SRV 数
	/// @details 1 フレーム中に参照可能な SRV スロットの上限値。
	static const uint32_t kMaxSRVCount;
private:

	// Windows API 管理
	Engine::Base::WinApp* winApp_ = nullptr;
	HRESULT hr;
	// デバイス
	Microsoft::WRL::ComPtr< IDXGIFactory7> dxgiFactory = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	// コマンド
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	// スワップチェーン
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>swapChainResources;
	// 深度バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStenciResource;
	// ディスクリプタヒープ
	uint32_t descriptorSizeSRV;
	uint32_t descriptorSizeRTV;
	uint32_t descriptorSizeDSV;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;


	// RTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle;
	// ダブルバッファ用に 2 つの RTV を保持する
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvHandles;
	// Fence
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	HANDLE fenceEvent;
	uint64_t fenceValue = 0;
	// ビューポート
	D3D12_VIEWPORT viewport{};
	// シザー矩形
	D3D12_RECT scissorRect{};
	// DXC
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	IDxcIncludeHandler* includeHandler = nullptr;
	// バリア
	D3D12_RESOURCE_BARRIER barrier{};
	// FPS 固定用の基準時刻
	std::chrono::steady_clock::time_point reference_;

	

private:


	// FPS 固定初期化
	void InitializeFixFPS();
	// FPS 固定更新
	void UpdateFixFPS();




};

}

