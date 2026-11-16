#include "OffscreenRenderManager.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "HResult.h"
#include "SrvManager.h"
#include "WinApp.h"
#ifndef KUBO_RUNTIME_ONLY
#include "PipelineRootSignatureFactory.h"
#include <DirectXMath.h>
#include <cstring>
#endif
#include <cmath>
#include <cassert>
#include <imgui.h>
#include <string>
#include <stdexcept>

namespace Engine::Base {

namespace {

D3D12_VIEWPORT GameViewport(ViewExtent extent = {})
{
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(extent.width);
	viewport.Height = static_cast<float>(extent.height);
	viewport.MaxDepth = 1.0f;
	return viewport;
}

D3D12_RECT GameScissor(ViewExtent extent = {})
{
	return { 0, 0, static_cast<LONG>(extent.width), static_cast<LONG>(extent.height) };
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateGameDepth(DirectXCommon& graphics,ViewExtent extent)
{
    D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=extent.width;description.Height=extent.height;description.DepthOrArraySize=1;description.MipLevels=1;
    description.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;description.SampleDesc.Count=1;description.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};clear.Format=description.Format;clear.DepthStencil.Depth=1;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    ThrowIfFailed(graphics.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Game output depth resource");
    return depth;
}

}

OffscreenRenderManager* OffscreenRenderManager::instance_ = nullptr;

OffscreenRenderManager::OffscreenRenderManager() = default;
OffscreenRenderManager::~OffscreenRenderManager()
{
	Finalize();
	if (instance_ == this) {
		instance_ = nullptr;
	}
}

std::shared_ptr<DirectXCommon> OffscreenRenderManager::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

void OffscreenRenderManager::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager, ViewExtent gameExtent)
{
	if(!ValidGameOutputExtent(gameExtent)) throw std::invalid_argument("Invalid Game output extent");
	gameOutputExtent_=gameExtent;
	instance_ = this;
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

	//RTVの作成

	renderTargetTextureResource = CreateRenderTargetTextureResource(
		gameOutputExtent_.width,
		gameOutputExtent_.height,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		ViewAppearance{}.background,false
	);
	GetDirectXCommon()->TrackResourceState(
		renderTargetTextureResource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	renderTargetTextureHandle = GetDirectXCommon()->GetRTVCPUDescriptorHandle(2);

	GetDirectXCommon()->GetDevice()->CreateRenderTargetView(renderTargetTextureResource.Get(),
		&GetDirectXCommon()->GetRTVDesc(), renderTargetTextureHandle);
	gameDepth_=CreateGameDepth(*GetDirectXCommon(),gameOutputExtent_);
	gameDsv_=GetDirectXCommon()->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
	GetDirectXCommon()->GetDevice()->CreateDepthStencilView(gameDepth_.Get(),nullptr,gameDsv_->GetCPUDescriptorHandleForHeapStart());
	GetDirectXCommon()->TrackResourceState(gameDepth_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);


	//SRVの作成
	srvIndex = srvManager_->Allocate();
	srvManager_->CreateSRVforTexture2D(srvIndex, renderTargetTextureResource.Get(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, renderTargetMetadata_);

	//graphicsPipelineの初期化
	graphicsPipeline_ = std::make_unique<GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon);

	graphicsPipeline_->CreateAllPostEffects(); // ←これだけ！
}

void OffscreenRenderManager::Finalize()
{
    ResetPostTarget(gamePost_);
    ResetPostTarget(editorPost_);
	if (srvManager_ && editorViewSrv_ != UINT32_MAX) srvManager_->Free(editorViewSrv_);
	editorViewSrv_ = UINT32_MAX; editorViewGpu_ = {};
	imGuiEditorViewCpu_ = {}; imGuiEditorViewGpu_ = {};
	if (const auto graphics = dxCommon_.lock()) {
		graphics->UntrackResourceState(gameDepth_.Get());
		graphics->UntrackResourceState(editorViewColor_.Get());
		graphics->UntrackResourceState(editorViewDepth_.Get());
	}
	editorViewColor_.Reset(); editorViewDepth_.Reset(); editorViewRtv_.Reset(); editorViewDsv_.Reset();
	gameDepth_.Reset();gameDsv_.Reset();
	if (srvManager_ && srvIndex != UINT32_MAX) {
		srvManager_->Free(srvIndex);
		srvIndex = UINT32_MAX;
	}
	graphicsPipeline_.reset();
	if (dxCommon_.lock() && renderTargetTextureResource) {
		GetDirectXCommon()->UntrackResourceState(renderTargetTextureResource.Get());
	}
	renderTargetTextureResource.Reset();
	imGuiSceneTextureReady_ = false;
	dxCommon_.reset();
	srvManager_ = nullptr;
}

void OffscreenRenderManager::Begin()
{
	GetDirectXCommon()->TransitionResource(
		renderTargetTextureResource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	//描画先のRTVとDSVを設定する
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = gameDsv_->GetCPUDescriptorHandleForHeapStart();
	GetDirectXCommon()->GetCommandList()->OMSetRenderTargets(1, &renderTargetTextureHandle, false, &dsvHandle);
	//指定した色で画面全体をクリアする
	const float clearColor[] = { appearance_.background.x,appearance_.background.y,appearance_.background.z,appearance_.background.w };
	GetDirectXCommon()->GetCommandList()->ClearRenderTargetView(renderTargetTextureHandle, clearColor, 0, nullptr);
	GetDirectXCommon()->GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);



	const D3D12_VIEWPORT viewport = GameViewport(gameOutputExtent_);
	const D3D12_RECT scissorRect = GameScissor(gameOutputExtent_);

	GetDirectXCommon()->GetCommandList()->RSSetViewports(1, &viewport);
	GetDirectXCommon()->GetCommandList()->RSSetScissorRects(1, &scissorRect);


}

void OffscreenRenderManager::InitializeEditorView()
{
	if (HasEditorView()) return;
	ResizeEditorView({});
}

void OffscreenRenderManager::ResizeEditorView(ViewExtent extent)
{
	if (!extent.width || !extent.height || extent.width > 2048 || extent.height > 2048)
		throw std::invalid_argument("Editor view extent must be within 1..2048");
	if (HasEditorView() && extent == editorViewExtent_) return;
	auto graphics = GetDirectXCommon();
	auto color = CreateRenderTargetTextureResource(extent.width,extent.height,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,ViewAppearance{}.background,false);
	auto rtv = graphics->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
	auto dsv = graphics->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
	graphics->GetDevice()->CreateRenderTargetView(color.Get(),&graphics->GetRTVDesc(),rtv->GetCPUDescriptorHandleForHeapStart());
	D3D12_RESOURCE_DESC description{};
	description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; description.Width=extent.width; description.Height=extent.height;
	description.DepthOrArraySize=1; description.MipLevels=1; description.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
	description.SampleDesc.Count=1; description.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clear{}; clear.Format=description.Format; clear.DepthStencil.Depth=1;
	Microsoft::WRL::ComPtr<ID3D12Resource> depth;
	ThrowIfFailed(graphics->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Editor Scene depth resource");
	graphics->GetDevice()->CreateDepthStencilView(depth.Get(),nullptr,dsv->GetCPUDescriptorHandleForHeapStart());
	// Allocate candidates before changing live descriptors. Pending uploads/draws
	// must be submitted too: waiting only for prior frames is not sufficient.
	if (HasEditorView()) { graphics->CommandKick(); graphics->WaitForAllFrames(); }
    ResetPostTarget(editorPost_);
	const bool replacing = HasEditorView();
	const auto index = replacing ? editorViewSrv_ : srvManager_->Allocate();
	try { srvManager_->CreateSRVforTexture2D(index,color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,1,renderTargetMetadata_); }
	catch (...) { if (!replacing) srvManager_->Free(index); throw; }
	graphics->UntrackResourceState(editorViewColor_.Get());
	graphics->UntrackResourceState(editorViewDepth_.Get());
	editorViewColor_=std::move(color); editorViewDepth_=std::move(depth); editorViewRtv_=std::move(rtv); editorViewDsv_=std::move(dsv);
	editorViewExtent_=extent;
	editorViewSrv_=index; editorViewGpu_=srvManager_->GetGPUDescriptorHandle(index);
	graphics->TrackResourceState(editorViewColor_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
	graphics->TrackResourceState(editorViewDepth_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
	if (imGuiEditorViewCpu_.ptr) SetImGuiEditorViewSlot(imGuiEditorViewCpu_,imGuiEditorViewGpu_);
}

void OffscreenRenderManager::SetImGuiEditorViewSlot(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
	imGuiEditorViewCpu_=cpu; imGuiEditorViewGpu_=gpu;
	if (!editorViewColor_ || !cpu.ptr) return;
	D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
	desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; desc.Texture2D.MipLevels=1;
	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(editorViewColor_.Get(),&desc,cpu);
}

void OffscreenRenderManager::BeginEditorView()
{
    BindEditorView();
    auto* commands = GetDirectXCommon()->GetCommandList();
    const auto rtv = editorViewRtv_->GetCPUDescriptorHandleForHeapStart();
    const auto dsv = editorViewDsv_->GetCPUDescriptorHandleForHeapStart();
    const float clear[]{appearance_.background.x,appearance_.background.y,appearance_.background.z,appearance_.background.w};
    commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
}

void OffscreenRenderManager::BindEditorView()
{
    if (!HasEditorView()) throw std::logic_error("Editor view is not initialized");
    auto graphics = GetDirectXCommon();
    auto* commands = graphics->GetCommandList();
    graphics->TransitionResource(editorViewColor_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto rtv = editorViewRtv_->GetCPUDescriptorHandleForHeapStart();
    const auto dsv = editorViewDsv_->GetCPUDescriptorHandleForHeapStart();
    commands->OMSetRenderTargets(1,&rtv,false,&dsv);
    const auto viewport = GameViewport(editorViewExtent_);
    const auto scissor = GameScissor(editorViewExtent_);
    commands->RSSetViewports(1,&viewport);
    commands->RSSetScissorRects(1,&scissor);
}

void OffscreenRenderManager::EndEditorView(bool applyPost)
{
    if(applyPost)FinishView(editorViewColor_.Get(),editorViewSrv_,editorViewExtent_,editorPost_);
    else GetDirectXCommon()->TransitionResource(editorViewColor_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

#ifndef KUBO_RUNTIME_ONLY
void OffscreenRenderManager::DrawEditorGrid(const float (&viewProjection)[4][4])
{
    using namespace DirectX;
    struct CameraConstants { XMFLOAT4X4 viewProjection, inverse; } camera{};
    static_assert(sizeof(CameraConstants)==32*sizeof(float));
    std::memcpy(camera.viewProjection.m,viewProjection,sizeof(viewProjection));
    XMVECTOR determinant;
    const auto inverse = XMMatrixInverse(&determinant,XMLoadFloat4x4(&camera.viewProjection));
    const auto det = XMVectorGetX(determinant);
    if (!std::isfinite(det) || det==0 || XMMatrixIsNaN(inverse) || XMMatrixIsInfinite(inverse)) return;
    XMStoreFloat4x4(&camera.inverse,inverse);

    auto graphics = GetDirectXCommon();
    constexpr auto key = "Editor.SceneGrid";
    const auto* pipeline = graphicsPipeline_->FindPipeline(key);
    if (!pipeline) {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        CreateEditorGridRootSignature(*graphics,root.GetAddressOf());
        graphicsPipeline_->CreatePipeline({key,root.Get(),
            L"EditorResources/Shaders/SceneGrid.VS.hlsl",L"EditorResources/Shaders/SceneGrid.PS.hlsl",
            GraphicsPipeline::PipelineInputLayout::Empty,GraphicsPipeline::PipelineBlendMode::Alpha,
            GraphicsPipeline::PipelineDepthMode::ReadOnly});
        pipeline = graphicsPipeline_->FindPipeline(key);
    }
    auto* commands = graphics->GetCommandList();
    commands->SetGraphicsRootSignature(pipeline->rootSignature.Get());
    commands->SetPipelineState(pipeline->pipelineState.Get());
    commands->SetGraphicsRoot32BitConstants(0,32,&camera,0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3,1,0,0);
}

void OffscreenRenderManager::DrawEditorPreview(D3D12_CPU_DESCRIPTOR_HANDLE target, ViewExtent extent)
{
    // Expand the viewport along the longer axis and clip it to the output,
    // preserving the center of the Scene view without stretching it.
    const float scale = (std::max)(float(extent.width) / editorViewExtent_.width,
        float(extent.height) / editorViewExtent_.height);
    auto viewport = GameViewport(extent);
    viewport.Width = editorViewExtent_.width * scale;
    viewport.Height = editorViewExtent_.height * scale;
    viewport.TopLeftX = (extent.width - viewport.Width) * 0.5f;
    viewport.TopLeftY = (extent.height - viewport.Height) * 0.5f;
    const auto scissor = GameScissor(extent);
    auto* commands = GetDirectXCommon()->GetCommandList();
    commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    srvManager_->PreDraw();
    DrawPostEffect(editorViewSrv_,PostEffectType::Fullscreen,1);
}
#endif

void OffscreenRenderManager::BindRenderTarget()
{
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		gameDsv_->GetCPUDescriptorHandleForHeapStart();
	GetDirectXCommon()->GetCommandList()->OMSetRenderTargets(
		1, &renderTargetTextureHandle, false, &dsvHandle);
	const D3D12_VIEWPORT viewport = GameViewport(gameOutputExtent_);
	const D3D12_RECT scissorRect = GameScissor(gameOutputExtent_);
	GetDirectXCommon()->GetCommandList()->RSSetViewports(1, &viewport);
	GetDirectXCommon()->GetCommandList()->RSSetScissorRects(1, &scissorRect);
}

void OffscreenRenderManager::End()
{
    FinishView(renderTargetTextureResource.Get(),srvIndex,gameOutputExtent_,gamePost_);
}

void OffscreenRenderManager::ResetPostTarget(PostTarget& target)
{
    if(target.color)if(const auto graphics=dxCommon_.lock())graphics->UntrackResourceState(target.color.Get());
    target={};
}

void OffscreenRenderManager::FinishView(ID3D12Resource* source,uint32_t sourceSrv,ViewExtent extent,PostTarget& target)
{
    auto graphics=GetDirectXCommon();
    graphics->TransitionResource(source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if(appearance_.effect==PostEffectType::Fullscreen && appearance_.exposure==0)return;
    if(!target.color){
        PostTarget prepared;
        prepared.color=CreateRenderTargetTextureResource(extent.width,extent.height,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            ViewAppearance{}.background,false);
        prepared.rtv=graphics->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
        graphics->GetDevice()->CreateRenderTargetView(prepared.color.Get(),&graphics->GetRTVDesc(),
            prepared.rtv->GetCPUDescriptorHandleForHeapStart());
        graphics->TrackResourceState(prepared.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        target=std::move(prepared);
    }
    graphics->TransitionResource(target.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto* commands=graphics->GetCommandList();
    const auto rtv=target.rtv->GetCPUDescriptorHandleForHeapStart();
    const auto viewport=GameViewport(extent);
    const auto scissor=GameScissor(extent);
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    commands->RSSetViewports(1,&viewport);
    commands->RSSetScissorRects(1,&scissor);
    srvManager_->PreDraw();
    DrawPostEffect(sourceSrv,appearance_.effect,std::exp2(appearance_.exposure));

    // Publish through the original texture and stable ImGui/engine SRV.
    graphics->TransitionResource(target.color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    graphics->TransitionResource(source,D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyResource(source,target.color.Get());
    graphics->TransitionResource(source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void OffscreenRenderManager::DrawPostEffect(uint32_t sourceSrv,PostEffectType effect,float exposureScale)
{
    auto* commands=GetDirectXCommon()->GetCommandList();
    commands->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateCopyImageHandle(effect).Get());
    commands->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureHandle("PostEffect."+std::to_string(static_cast<int>(effect))).Get());
    commands->SetGraphicsRoot32BitConstants(1,1,&exposureScale,0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    srvManager_->SetGraphicsRootDescriptorTable(0,sourceSrv);
    commands->DrawInstanced(3,1,0,0);
}

void OffscreenRenderManager::Draw()
{
    // Fit the authored output inside the fixed window instead of stretching it.
    const auto rect=GamePresentationRect({WinApp::kClientWidth,WinApp::kClientHeight},gameOutputExtent_);
    auto viewport=GameViewport({rect.width,rect.height});viewport.TopLeftX=float(rect.left);viewport.TopLeftY=float(rect.top);
    const D3D12_RECT scissor{static_cast<LONG>(rect.left),static_cast<LONG>(rect.top),static_cast<LONG>(rect.left+rect.width),static_cast<LONG>(rect.top+rect.height)};
    GetDirectXCommon()->GetCommandList()->RSSetViewports(1,&viewport);
    GetDirectXCommon()->GetCommandList()->RSSetScissorRects(1,&scissor);
    DrawPostEffect(srvIndex,currentEffectType_,1);




}

void OffscreenRenderManager::SetScenePostEffectType(PostEffectType type)
{
	sceneEffectType_ = type;
	if (autoSceneEffectEnabled_) {
		currentEffectType_ = type;
	}
}

void OffscreenRenderManager::CreateImGuiSceneTextureSrv(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
	imGuiSceneTextureCpuHandle = cpuHandle;
	imGuiSceneTextureGpuHandle_ = gpuHandle;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(
		renderTargetTextureResource.Get(),
		&srvDesc,
		imGuiSceneTextureCpuHandle);
	imGuiSceneTextureReady_ = true;
}



Microsoft::WRL::ComPtr<ID3D12Resource> OffscreenRenderManager::CreateRenderTargetTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, const Vector4& clearColor, bool optimizeClear)
{
	D3D12_RESOURCE_DESC resourceDesc{ };
	resourceDesc.Width = width;//Textureの幅
	resourceDesc.Height = height;//Textureの高さ
	resourceDesc.MipLevels = 1;//mipmapの数
	resourceDesc.DepthOrArraySize = 1;//奥行きまたは配列テクスチャの配列数
	resourceDesc.Format = format;//Textureのフォーマット
	resourceDesc.SampleDesc.Count = 1;//サンプリクト。１固定。
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;//RenderTargetとして使う
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;//Textureの次元数。普段使っているのは２次元

	//利用するHeapの設定。非常に特殊な運用。
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//細かい設定を行う


	// クリア値の設定
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;
	clearValue.Color[0] = clearColor.x;
	clearValue.Color[1] = clearColor.y;
	clearValue.Color[2] = clearColor.z;
	clearValue.Color[3] = clearColor.w;

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = GetDirectXCommon()->GetDevice()->CreateCommittedResource(
		&heapProperties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊設定。特になし
		&resourceDesc,//Resourceの設定
		D3D12_RESOURCE_STATE_RENDER_TARGET,//RenderTargetとして使う状態にしておく
		optimizeClear?&clearValue:nullptr,
		IID_PPV_ARGS(&resource)//作成するResourceポインタへのポインタ
	);
	ThrowIfFailed(
		hr,
		"ID3D12Device::CreateCommittedResource offscreen target");

	return resource;
}

#ifndef KUBO_RUNTIME_ONLY
void OffscreenRenderManager::DrawImGui(bool* open)
{
	if (open && !*open) {
		return;
	}

	if (!ImGui::Begin("オフスクリーン設定", open)) {
		ImGui::End();
		return;
	}
	ImGui::Text(
		"Tracked GPU Resource States: %zu",
		dxCommon_.lock() ? GetDirectXCommon()->GetTrackedResourceStateCount() : size_t{ 0 });

	const char* items[] = {
	   "Fullscreen",
	   "Grayscale",
	   "Vignette",
	   "BoxFilter",
	   "LuminanceOutline",
	   "RadialBlur"
	};

	if (ImGui::Checkbox("Auto Scene Effect", &autoSceneEffectEnabled_) && autoSceneEffectEnabled_) {
		currentEffectType_ = sceneEffectType_;
	}

	// 現在の enum を int に変換
	int current = static_cast<int>(currentEffectType_);

	if (ImGui::Combo("Post Effect", &current, items, IM_ARRAYSIZE(items))) {
		// 変更があった場合は enum にキャストして設定
		autoSceneEffectEnabled_ = false;
		SetPostEffectType(static_cast<PostEffectType>(current));
	}
	ImGui::End();
}
#endif

}
