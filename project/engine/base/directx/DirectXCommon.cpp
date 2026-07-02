#include "DirectXCommon.h"
#include "HResult.h"
#include "ShaderCompiler.h"
#include "WinApp.h"
#include <algorithm>
#include <cassert>
#include <format>
#include <limits>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#include "Logger.h"
#include "StringUtility.h"

namespace {

constexpr float kMicrosecondsPerSecond = 1000000.0f;
constexpr float kMinTargetFrameRate = 15.0f;
constexpr float kMaxTargetFrameRate = 1000.0f;
constexpr float kDefaultClearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

double P95(std::vector<double> samples)
{
	if (samples.empty()) {
		return 0.0;
	}
	std::sort(samples.begin(), samples.end());
	const size_t index = ((samples.size() * 95 + 99) / 100) - 1;
	return samples[index];
}

double Maximum(const std::vector<double>& samples)
{
	return samples.empty()
		? 0.0
		: *std::max_element(samples.begin(), samples.end());
}

}

namespace Engine::Base {

DirectXCommon::~DirectXCommon()
{
	WaitForAllFrames();
	if (gpuTimestampReadback_ && gpuTimestampCpuData_) {
		gpuTimestampReadback_->Unmap(0, nullptr);
		gpuTimestampCpuData_ = nullptr;
	}
	for (FrameContext& frameContext : frameContexts_) {
		if (frameContext.uploadArena && frameContext.uploadCpuAddress) {
			frameContext.uploadArena->Unmap(0, nullptr);
			frameContext.uploadCpuAddress = nullptr;
		}
	}
	if (fenceEvent) {
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}

void DirectXCommon::EnableDebugLayer()
{
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーと GPU バリデーションを有効にする
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif
}

Microsoft::WRL::ComPtr<IDXGIAdapter4> DirectXCommon::SelectAdapter()
{
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(
		i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; i++) {

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		ThrowIfFailed(hr, "IDXGIAdapter4::GetDesc3");

		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			Logger::Log(StringUtility::ConvertString(std::format(L"Use Adapater:{}\n", adapterDesc.Description)));
			return useAdapter;
		}
		useAdapter = nullptr;
	}

	return nullptr;
}

void DirectXCommon::CreateDevice(IDXGIAdapter4* adapter)
{
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(adapter, featureLevels[i], IID_PPV_ARGS(&device));
		if (SUCCEEDED(hr)) {
			Logger::Log(std::format("FeatureLevel:{}\n", featureLevelStrings[i]));
			break;
		}
	}

	if (!device) {
		ThrowIfFailed(hr, "D3D12CreateDevice");
	}
	Logger::Log("Complete create D3D12Device!!!\n");
}

void DirectXCommon::ConfigureInfoQueue()
{
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;

	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// 破損レベルだけ即停止する。ERROR は Output に残すが、移行中の Scene 切り替え検証を止めない。
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
		// WARNING で毎回停止すると、終了時の live object レポートなどでも 0x87A が飛ぶ。
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);

		D3D12_MESSAGE_ID denyIds[] = {
			// Windows 11 環境で発生する既知の過剰メッセージを抑制する
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif
}

void DirectXCommon::DeviceInitialize()
{
	EnableDebugLayer();

#pragma region DxgiFactory
	// DXGI ファクトリを作り、以後のアダプタ列挙とスワップチェーン生成の起点にする

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	ThrowIfFailed(hr, "CreateDXGIFactory");
#pragma endregion

	// ハードウェアアダプタを選び、そのアダプタ上に D3D12 デバイスを作成する
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = SelectAdapter();
	if (!useAdapter) {
		throw std::runtime_error(
			"DirectXCommon could not find a hardware adapter");
	}

	CreateDevice(useAdapter.Get());
	ConfigureInfoQueue();

}

void DirectXCommon::CommandInitialize()
{
#pragma region CommandQueue,CommandAllocator,CommandList
	//コマンドキューを生成する
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	ThrowIfFailed(hr, "ID3D12Device::CreateCommandQueue");

	// バックバッファごとに独立したコマンドアロケーターを持つ
	for (FrameContext& frameContext : frameContexts_) {
		hr = device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&frameContext.commandAllocator));
		ThrowIfFailed(hr, "ID3D12Device::CreateCommandAllocator");
	}
	InitializeFrameUploadArenas();

	//コマンドリストを生成する
	hr = device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		frameContexts_[currentFrameIndex_].commandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList));
	ThrowIfFailed(hr, "ID3D12Device::CreateCommandList");
#pragma endregion

}

void DirectXCommon::SwapChainInitialize()
{
#pragma region SwapChain
	// ウィンドウと同サイズのダブルバッファを作り、Present 先を確保する

	swapChainDesc.Width = Engine::Base::WinApp::kClientWidth;		//画面の幅。ウィンドウのクライアント領域を同じ物にしておく
	swapChainDesc.Height = Engine::Base::WinApp::kClientHeight;		//画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	//色の形式	
	swapChainDesc.SampleDesc.Count = 1;//マルチサンプルなし
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;//描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;//ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;//モニタに写したら、中身を破壊
	//コマンドキュー、ウィンドウハンドル、設定渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	ThrowIfFailed(hr, "IDXGIFactory::CreateSwapChainForHwnd");
	currentFrameIndex_ = swapChain->GetCurrentBackBufferIndex();

#pragma endregion 


}

void DirectXCommon::DepthBufferInitialize()
{
	// 深度バッファは毎フレームの 3D 描画で共通利用するため、画面サイズ固定で 1 枚確保する
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = Engine::Base::WinApp::kClientWidth;//Textureの幅
	resourceDesc.Height = Engine::Base::WinApp::kClientHeight;//Textureの高さ
	resourceDesc.MipLevels = 1;//mipmapの数
	resourceDesc.DepthOrArraySize = 1;//奥行きor配列Texturの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//DetpthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;//サンプリング。１固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;//2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;//DepthSrencilとして使う通知

	//利用するhepの設定
	D3D12_HEAP_PROPERTIES heapProperties{  };
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//VRAM上に作る
	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClerValue{};
	depthClerValue.DepthStencil.Depth = 1.0f;//最大値
	depthClerValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//フォーアット。Resource合わせる
	//Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	hr = device->CreateCommittedResource(
		&heapProperties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊設定。特になし
		&resourceDesc,//REesourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,//深度値を書き込む状態のしておく
		&depthClerValue,//Clear最適値
		IID_PPV_ARGS(&resource));//作成するResourceポインタへのポインタ
	ThrowIfFailed(hr, "ID3D12Device::CreateCommittedResource depth buffer");
	//DepthStencilTextureをウィンドウサイズで作成
	depthStenciResource = resource;

}

void DirectXCommon::DescriptorHeapInitialize()
{
	//サイズを取得
	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// SRV heap は SrvManager が単独で所有する
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, false);//RTV
	dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);//DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではない

}

void DirectXCommon::RTVInitialize()
{

	// スワップチェーンの各バックバッファへ RTV を張り、描画先として扱えるようにする

	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	ThrowIfFailed(hr, "IDXGISwapChain::GetBuffer 0");
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	ThrowIfFailed(hr, "IDXGISwapChain::GetBuffer 1");

#pragma region RTV
	//RTVの設定
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;//出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;//2dテクスチャとして書き込む
	//ディスクリプトの先頭を取得する
	rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	//まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = rtvStartHandle;

	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	//２つ目のディスクリプタハンドルを得る（自力で）
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//２つ目を作る
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);
#pragma endregion
}



void DirectXCommon::DSVInitialize()
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//Format
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;//2Dtexture
	//DSHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStenciResource.Get(),
		&dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

}

void DirectXCommon::FenceInitialize()
{

#pragma region Fence
	// CPU/GPU 同期に使う Fence と待機イベントを作成する

	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	ThrowIfFailed(hr, "ID3D12Device::CreateFence");

	//fenceのSignalを待つためのイベントを作成する
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!fenceEvent) {
		ThrowIfFailed(
			HRESULT_FROM_WIN32(GetLastError()),
			"CreateEvent fence");
	}

#pragma endregion

}

void DirectXCommon::ViewportInitialize()
{

	//クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = Engine::Base::WinApp::kClientWidth;
	viewport.Height = Engine::Base::WinApp::kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

}

void DirectXCommon::ScissorInitialize()
{

	//基本的にビューポートと同じ矩形が構成さるようにする
	scissorRect.left = 0;
	scissorRect.right = Engine::Base::WinApp::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = Engine::Base::WinApp::kClientHeight;

}

void DirectXCommon::ImguiInitialize()
{
}

void DirectXCommon::InitializeGraphicsResources()
{
	// 描画基盤を依存順に初期化し、後段のリソース生成が前段の結果に依存できるようにする
	DeviceInitialize();
	CommandInitialize();
	InitializeGpuTiming();
	SwapChainInitialize();
	DepthBufferInitialize();
	DescriptorHeapInitialize();
	RTVInitialize();
	DSVInitialize();
	FenceInitialize();
	ViewportInitialize();
	ScissorInitialize();
	shaderCompiler_ = std::make_unique<ShaderCompiler>();
	shaderCompiler_->Initialize();
	ImguiInitialize();
}

//初期化
void DirectXCommon::Initialize(Engine::Base::WinApp* winApp)
{
	assert(winApp);//NULL検出
	winApp_ = winApp;

	// フレーム制御と描画基盤を順序どおりに初期化する
	InitializeFixFPS();
	InitializeGraphicsResources();
}

void DirectXCommon::PrepareBackBufferForRendering(uint32_t backBufferIndex)
{
	// Present 状態のバックバッファを描画可能状態へ遷移する
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrier);

	// 描画先の RTV を設定し、描画開始前にクリアする
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], kDefaultClearColor, 0, nullptr);
}

void DirectXCommon::FinalizeFrameTransition()
{
	// 描画完了後は Present 用状態へ戻してから実行キューへ送る
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);
	CloseAndExecuteCommandList();
	ThrowIfFailed(
		swapChain->Present(vSyncEnabled_ ? 1 : 0, 0),
		"IDXGISwapChain::Present");
}

void DirectXCommon::CloseAndExecuteCommandList()
{
	hr = commandList->Close();
	ThrowIfFailed(hr, "ID3D12GraphicsCommandList::Close");
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);
}

void DirectXCommon::WaitForGpuCompletion()
{
	FrameContext& frameContext = frameContexts_[currentFrameIndex_];
	SignalFrame(frameContext);
	WaitForFrame(frameContext);
}

void DirectXCommon::WaitForAllFrames()
{
	if (!fence || !fenceEvent) {
		return;
	}
	for (const FrameContext& frameContext : frameContexts_) {
		WaitForFrame(frameContext);
	}
}

void DirectXCommon::InitializeFrameUploadArenas()
{
	for (FrameContext& frameContext : frameContexts_) {
		frameContext.uploadArena = CreateBufferResource(kFrameUploadArenaSize);
		frameContext.uploadCpuAddress = MapResource<std::byte>(
			frameContext.uploadArena.Get(),
			"ID3D12Resource::Map frame upload arena");
		frameContext.uploadOffset = 0;
	}
}

void DirectXCommon::SignalFrame(FrameContext& frameContext)
{
	frameContext.fenceValue = ++nextFenceValue_;
	ThrowIfFailed(
		commandQueue->Signal(fence.Get(), frameContext.fenceValue),
		"ID3D12CommandQueue::Signal");
}

void DirectXCommon::WaitForFrame(const FrameContext& frameContext)
{
	if (frameContext.fenceValue == 0 ||
		fence->GetCompletedValue() >= frameContext.fenceValue) {
		return;
	}

	ThrowIfFailed(
		fence->SetEventOnCompletion(frameContext.fenceValue, fenceEvent),
		"ID3D12Fence::SetEventOnCompletion");
	const DWORD waitResult = WaitForSingleObject(fenceEvent, INFINITE);
	if (waitResult != WAIT_OBJECT_0) {
		ThrowIfFailed(
			HRESULT_FROM_WIN32(GetLastError()),
			"WaitForSingleObject fence");
	}
}

void DirectXCommon::ResetCommandObjects(uint32_t frameIndex)
{
	FrameContext& frameContext = frameContexts_[frameIndex];
	hr = frameContext.commandAllocator->Reset();
	ThrowIfFailed(hr, "ID3D12CommandAllocator::Reset");
	hr = commandList->Reset(frameContext.commandAllocator.Get(), nullptr);
	ThrowIfFailed(hr, "ID3D12GraphicsCommandList::Reset");
	frameContext.uploadOffset = 0;
	frameContext.deferredReleaseResources.clear();
}



void DirectXCommon::Begin()
{

	//これから書き込むバックバッファのインデックスを取得する
	currentFrameIndex_ = swapChain->GetCurrentBackBufferIndex();
	PrepareBackBufferForRendering(currentFrameIndex_);

	// 以降の描画がウィンドウ全体へ正しく出るようビューポートとシザーを固定する
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}



void DirectXCommon::End()
{
	FrameContext& submittedFrame = frameContexts_[currentFrameIndex_];
	const uint32_t timestampBase = currentFrameIndex_ * 4;
	commandList->EndQuery(
		gpuTimestampQueryHeap_.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		timestampBase + 1);
	commandList->ResolveQueryData(
		gpuTimestampQueryHeap_.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		timestampBase,
		4,
		gpuTimestampReadback_.Get(),
		static_cast<uint64_t>(timestampBase) * sizeof(uint64_t));
	submittedFrame.gpuTimestampSubmitted = true;
	submittedFrame.shadowTimestampSubmitted =
		submittedFrame.shadowTimestampRecording;
	FinalizeFrameTransition();
	SignalFrame(submittedFrame);
	UpdateFixFPS();
	currentFrameIndex_ = swapChain->GetCurrentBackBufferIndex();
	WaitForFrame(frameContexts_[currentFrameIndex_]);
	ResetCommandObjects(currentFrameIndex_);
}

void DirectXCommon::BeginGpuFrameTiming()
{
	CollectCompletedGpuTiming(currentFrameIndex_);
	FrameContext& frameContext = frameContexts_[currentFrameIndex_];
	frameContext.shadowTimestampRecording = false;
	const uint32_t timestampBase = currentFrameIndex_ * 4;
	commandList->EndQuery(
		gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase);
}

void DirectXCommon::InitializeGpuTiming()
{
	D3D12_QUERY_HEAP_DESC queryHeapDesc{};
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.Count = kFrameCount * 4;
	ThrowIfFailed(
		device->CreateQueryHeap(
			&queryHeapDesc, IID_PPV_ARGS(&gpuTimestampQueryHeap_)),
		"ID3D12Device::CreateQueryHeap GPU timing");

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(uint64_t) * queryHeapDesc.Count;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ThrowIfFailed(
		device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&gpuTimestampReadback_)),
		"ID3D12Device::CreateCommittedResource GPU timing readback");
	ThrowIfFailed(
		gpuTimestampReadback_->Map(
			0, nullptr, reinterpret_cast<void**>(&gpuTimestampCpuData_)),
		"ID3D12Resource::Map GPU timing readback");
	ThrowIfFailed(
		commandQueue->GetTimestampFrequency(&gpuTimestampFrequency_),
		"ID3D12CommandQueue::GetTimestampFrequency");
}

void DirectXCommon::CollectCompletedGpuTiming(uint32_t frameIndex)
{
	FrameContext& frameContext = frameContexts_[frameIndex];
	if (!frameContext.gpuTimestampSubmitted || !gpuTimestampCpuData_ ||
		gpuTimestampFrequency_ == 0) {
		return;
	}
	const uint32_t timestampBase = frameIndex * 4;
	const double millisecondsPerTick =
		1000.0 / static_cast<double>(gpuTimestampFrequency_);
	const uint64_t frameStart = gpuTimestampCpuData_[timestampBase];
	const uint64_t frameEnd = gpuTimestampCpuData_[timestampBase + 1];
	if (frameEnd >= frameStart) {
		const double frameMilliseconds =
			static_cast<double>(frameEnd - frameStart) * millisecondsPerTick;
		frameGpuMillisecondsTotal_ += frameMilliseconds;
		frameGpuMillisecondsSamples_.push_back(frameMilliseconds);
		++frameGpuSampleCount_;
	}
	if (frameContext.shadowTimestampSubmitted) {
		const uint64_t shadowStart = gpuTimestampCpuData_[timestampBase + 2];
		const uint64_t shadowEnd = gpuTimestampCpuData_[timestampBase + 3];
		if (shadowEnd >= shadowStart) {
			const double shadowMilliseconds =
				static_cast<double>(shadowEnd - shadowStart) * millisecondsPerTick;
			shadowGpuMillisecondsTotal_ += shadowMilliseconds;
			shadowGpuMillisecondsSamples_.push_back(shadowMilliseconds);
			++shadowGpuSampleCount_;
		}
	}
	frameContext.gpuTimestampSubmitted = false;
}

void DirectXCommon::BeginShadowGpuTiming()
{
	const uint32_t timestampBase = currentFrameIndex_ * 4;
	commandList->EndQuery(
		gpuTimestampQueryHeap_.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		timestampBase + 2);
	frameContexts_[currentFrameIndex_].shadowTimestampRecording = true;
}

void DirectXCommon::EndShadowGpuTiming()
{
	const uint32_t timestampBase = currentFrameIndex_ * 4;
	commandList->EndQuery(
		gpuTimestampQueryHeap_.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		timestampBase + 3);
}

void DirectXCommon::ResetGpuTimingStatistics()
{
	frameGpuMillisecondsTotal_ = 0.0;
	shadowGpuMillisecondsTotal_ = 0.0;
	frameGpuSampleCount_ = 0;
	shadowGpuSampleCount_ = 0;
	frameGpuMillisecondsSamples_.clear();
	shadowGpuMillisecondsSamples_.clear();
}

double DirectXCommon::GetAverageFrameGpuMilliseconds() const
{
	return frameGpuSampleCount_ > 0
		? frameGpuMillisecondsTotal_ / static_cast<double>(frameGpuSampleCount_)
		: 0.0;
}

double DirectXCommon::GetAverageShadowGpuMilliseconds() const
{
	return shadowGpuSampleCount_ > 0
		? shadowGpuMillisecondsTotal_ / static_cast<double>(shadowGpuSampleCount_)
		: 0.0;
}

double DirectXCommon::GetFrameGpuP95Milliseconds() const
{
	return P95(frameGpuMillisecondsSamples_);
}

double DirectXCommon::GetShadowGpuP95Milliseconds() const
{
	return P95(shadowGpuMillisecondsSamples_);
}

double DirectXCommon::GetFrameGpuMaxMilliseconds() const
{
	return Maximum(frameGpuMillisecondsSamples_);
}

double DirectXCommon::GetShadowGpuMaxMilliseconds() const
{
	return Maximum(shadowGpuMillisecondsSamples_);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index)
{
	return GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index)
{
	return GetGPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
}



Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heaptype, UINT numDescriptrs, bool shaderVisible)
{
	//ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heaptype;//レンダーターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescriptrs;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;//ダブルバッファ用に2つ。多くても別に構わない
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	ThrowIfFailed(hr, "ID3D12Device::CreateDescriptorHeap");
	return descriptorHeap;
}

void DirectXCommon::InitializeFixFPS()
{
	//現在時間を記録する
	reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS()
{
	if (!frameRateLimitEnabled_ || targetFrameRate_ <= 0.0f) {
		reference_ = std::chrono::steady_clock::now();
		return;
	}

	const std::chrono::microseconds targetFrameTime(
		uint64_t(kMicrosecondsPerSecond / targetFrameRate_));
	const auto nextFrameTime = reference_ + targetFrameTime;
	std::this_thread::sleep_until(nextFrameTime);

	const auto now = std::chrono::steady_clock::now();
	if (now > nextFrameTime + targetFrameTime) {
		reference_ = now;
	} else {
		reference_ = nextFrameTime;
	}
}

void DirectXCommon::SetFrameRateLimitEnabled(bool enabled)
{
	frameRateLimitEnabled_ = enabled;
	reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::SetTargetFrameRate(float frameRate)
{
	targetFrameRate_ = std::clamp(frameRate, kMinTargetFrameRate, kMaxTargetFrameRate);
	reference_ = std::chrono::steady_clock::now();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;

}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{

	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;

}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile)
{
	return shaderCompiler_->Compile(filePath, profile);
}




Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{

	//VertexResourceを作成
	//頂点リソース用ヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	//頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	//バッファーリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes;
	//バッファの場合はこれらには1する決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	//バッファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	//実際に頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));
	ThrowIfFailed(hr, "ID3D12Device::CreateCommittedResource buffer");

	return vertexResource;

}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateDefaultBufferResource(
	const void* data,
	size_t sizeInBytes,
	D3D12_RESOURCE_STATES finalState)
{
	if (!data || sizeInBytes == 0) {
		throw std::invalid_argument(
			"DirectXCommon::CreateDefaultBufferResource requires non-empty data");
	}

	D3D12_HEAP_PROPERTIES defaultHeapProperties{};
	defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> defaultResource;
	ThrowIfFailed(
		device->CreateCommittedResource(
			&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&defaultResource)),
		"ID3D12Device::CreateCommittedResource default buffer");

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource =
		CreateBufferResource(sizeInBytes);
	void* mappedData = MapResource<void>(
		uploadResource.Get(),
		"ID3D12Resource::Map default buffer staging");
	std::memcpy(mappedData, data, sizeInBytes);
	uploadResource->Unmap(0, nullptr);

	commandList->CopyBufferRegion(
		defaultResource.Get(),
		0,
		uploadResource.Get(),
		0,
		sizeInBytes);
	TransitionResource(
		defaultResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		finalState);
	frameContexts_[currentFrameIndex_].deferredReleaseResources.push_back(
		std::move(uploadResource));
	return defaultResource;
}

DirectXCommon::FrameUploadAllocation DirectXCommon::AllocateFrameUpload(
	size_t sizeInBytes,
	size_t alignment)
{
	if (sizeInBytes == 0) {
		throw std::invalid_argument(
			"DirectXCommon::AllocateFrameUpload requires a non-zero size");
	}
	if (alignment == 0) {
		throw std::invalid_argument(
			"DirectXCommon::AllocateFrameUpload alignment must be non-zero");
	}

	FrameContext& frameContext = frameContexts_[currentFrameIndex_];
	if (frameContext.uploadOffset >
		(std::numeric_limits<size_t>::max)() - (alignment - 1)) {
		throw std::overflow_error(
			"DirectXCommon::AllocateFrameUpload offset overflow");
	}
	const size_t alignedOffset =
		((frameContext.uploadOffset + alignment - 1) / alignment) * alignment;
	if (alignedOffset > kFrameUploadArenaSize ||
		sizeInBytes > kFrameUploadArenaSize - alignedOffset) {
		throw std::runtime_error(std::format(
			"Frame upload arena exhausted: requested {} bytes with {}-byte alignment, {} of {} bytes used",
			sizeInBytes,
			alignment,
			frameContext.uploadOffset,
			kFrameUploadArenaSize));
	}

	FrameUploadAllocation allocation{
		.cpuAddress = frameContext.uploadCpuAddress + alignedOffset,
		.gpuAddress =
			frameContext.uploadArena->GetGPUVirtualAddress() + alignedOffset,
		.resource = frameContext.uploadArena.Get(),
		.offset = alignedOffset,
		.size = sizeInBytes,
	};
	frameContext.uploadOffset = alignedOffset + sizeInBytes;
	return allocation;
}

void DirectXCommon::DeferResourceRelease(
	Microsoft::WRL::ComPtr<ID3D12Resource>&& resource)
{
	if (resource) {
		frameContexts_[currentFrameIndex_].deferredReleaseResources.push_back(
			std::move(resource));
	}
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata)
{
	// メタデータどおりのテクスチャをまず COPY_DEST で作り、後段の UploadTextureData で埋める
	D3D12_RESOURCE_DESC resourceDesc{ };
	resourceDesc.Width = UINT(metadata.width);//Textureの幅
	resourceDesc.Height = UINT(metadata.height);//Textureの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);//mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);//奥行きまたは配列テクスチャの配列数
	resourceDesc.Format = metadata.format;//Textureのフォーマット
	resourceDesc.SampleDesc.Count = 1;//サンプリクト。１固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);//Textureの次元数。普段使っているのは２次元
	//利用するHeapの設定。非常に特殊な運用。
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//細かい設定を行う
	//Resouceの作成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊設定。特になし
		&resourceDesc,//Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,//Clear最適値。使わないのでnullptr
		IID_PPV_ARGS(&resource));

	ThrowIfFailed(hr, "ID3D12Device::CreateCommittedResource texture");
	return resource;

}




Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData
(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	// CPU 側のミップ画像群を GPU アップロード用のサブリソース列へ変換する
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);
	UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// 転送完了後はシェーダーから参照できる状態へ戻す
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);


	return intermediateResource;
}

void DirectXCommon::CommandKick()
{
	// 初期化中に積んだコマンドを即時実行し、以後の生成処理で参照できる状態まで進める
	CloseAndExecuteCommandList();
	WaitForGpuCompletion();
	ResetCommandObjects(currentFrameIndex_);
}

void DirectXCommon::TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{




}

}








