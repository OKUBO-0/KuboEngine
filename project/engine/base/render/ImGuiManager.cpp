#include "ImGuiManager.h"
#include "DirectXCommon.h"
#include "DebugEditorManager.h"
#include "ImGuizmoManager.h"
#include "OffscreenRenderManager.h"
#include "WinApp.h"
#include <cassert>
#include "IconsFontAwesome5.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <implot.h>
#include <imgui_node_editor.h>

namespace Engine::Base {

namespace {

constexpr uint32_t kImGuiSrvDescriptorCount = 8;
constexpr uint32_t kImGuiSceneTextureSrvIndex = 0;
constexpr uint32_t kImGuiFirstDynamicSrvIndex = 1;

}

void ImGuiManager::AllocateSrvDescriptor(
	ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
	ImGuiManager* manager = static_cast<ImGuiManager*>(info->UserData);
	uint32_t index = 0;
	if (!manager->freeSrvDescriptorIndices_.empty()) {
		index = manager->freeSrvDescriptorIndices_.back();
		manager->freeSrvDescriptorIndices_.pop_back();
	} else {
		assert(manager->nextSrvDescriptorIndex_ < kImGuiSrvDescriptorCount);
		index = manager->nextSrvDescriptorIndex_++;
	}

	*outCpuHandle = manager->srvHeap_->GetCPUDescriptorHandleForHeapStart();
	outCpuHandle->ptr += static_cast<SIZE_T>(manager->srvDescriptorSize_) * index;
	*outGpuHandle = manager->srvHeap_->GetGPUDescriptorHandleForHeapStart();
	outGpuHandle->ptr += static_cast<UINT64>(manager->srvDescriptorSize_) * index;
}

void ImGuiManager::FreeSrvDescriptor(
	ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE)
{
	ImGuiManager* manager = static_cast<ImGuiManager*>(info->UserData);
	const D3D12_CPU_DESCRIPTOR_HANDLE start = manager->srvHeap_->GetCPUDescriptorHandleForHeapStart();
	const uint32_t index = static_cast<uint32_t>((cpuHandle.ptr - start.ptr) / manager->srvDescriptorSize_);
	manager->freeSrvDescriptorIndices_.push_back(index);
}

void ImGuiManager::Initialize(DirectXCommon* dxCommon, Engine::Base::WinApp* winApp)
{
#ifdef _DEBUG

	dxCommon_ = dxCommon;
	winApp_ = winApp;

	//imguiのコンテキストを生成
	ImGui::CreateContext();
	imPlotContext_ = ImPlot::CreateContext();
	nodeEditorContext_ = ax::NodeEditor::CreateEditor();
	ax::NodeEditor::SetCurrentEditor(nodeEditorContext_);
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImFontConfig fontConfig;
	fontConfig.SizePixels = 16.0f;
	fontConfig.Flags |= ImFontFlags_NoLoadError;
	ImFont* mainFont = io.Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\meiryo.ttc",
		16.0f,
		&fontConfig,
		io.Fonts->GetGlyphRangesJapanese());
	if (!mainFont) {
		ImFontConfig defaultFontConfig;
		defaultFontConfig.SizePixels = 13.0f;
		io.Fonts->AddFontDefault(&defaultFontConfig);
	}
	static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig iconConfig;
	iconConfig.MergeMode = true;
	iconConfig.PixelSnapH = true;
	iconConfig.GlyphMinAdvanceX = 16.0f;
	iconConfig.Flags |= ImFontFlags_NoLoadError;
	io.Fonts->AddFontFromFileTTF(
		"externals/IconFontCppHeaders/fa-solid-900.ttf",
		16.0f,
		&iconConfig,
		iconRanges);
	//imguiのスタイルを設定
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 2.0f;
	style.FrameRounding = 2.0f;
	style.TabRounding = 3.0f;
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 0.98f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.17f, 0.22f, 1.00f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.20f, 1.00f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.32f, 0.42f, 1.00f);
	style.Colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.27f, 0.36f, 1.00f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.30f, 0.40f, 1.00f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.29f, 0.36f, 0.48f, 1.00f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.32f, 0.42f, 1.00f);

	ImGui_ImplWin32_Init(winApp_->GetHwnd());
	//デスクリプタヒープ設定
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = kImGuiSrvDescriptorCount;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//デスクリプターフープ生成
	HRESULT hr = dxCommon_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
	assert(SUCCEEDED(hr));

	srvDescriptorSize_ = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	nextSrvDescriptorIndex_ = kImGuiFirstDynamicSrvIndex;
	freeSrvDescriptorIndices_.clear();
	defaultDockLayoutBuilt_ = false;

	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = dxCommon_->GetDevice();
	initInfo.CommandQueue = dxCommon_->GetCommandQueue();
	initInfo.NumFramesInFlight = static_cast<int>(dxCommon_->GetBackBufferCount());
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = srvHeap_.Get();
	initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
	initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;
	initInfo.UserData = this;
	ImGui_ImplDX12_Init(&initInfo);

	if (OffscreenRenderManager* offscreen = OffscreenRenderManager::GetInstance()) {
		D3D12_CPU_DESCRIPTOR_HANDLE sceneCpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
		sceneCpuHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * kImGuiSceneTextureSrvIndex;
		D3D12_GPU_DESCRIPTOR_HANDLE sceneGpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
		sceneGpuHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * kImGuiSceneTextureSrvIndex;
		offscreen->CreateImGuiSceneTextureSrv(sceneCpuHandle, sceneGpuHandle);
	}
#endif // _DEBUG


}

void ImGuiManager::Finalize()
{
#ifdef _DEBUG

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ax::NodeEditor::DestroyEditor(nodeEditorContext_);
	nodeEditorContext_ = nullptr;
	ImPlot::DestroyContext(imPlotContext_);
	imPlotContext_ = nullptr;
	ImGui::DestroyContext();

	srvHeap_.Reset();
#endif // _DEBUG


}

void ImGuiManager::Begin()
{
#ifdef _DEBUG

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	Engine::Editor::ImGuizmoManager::BeginFrame();
	const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
		0,
		ImGui::GetMainViewport(),
		ImGuiDockNodeFlags_None);
	if (!defaultDockLayoutBuilt_) {
		Engine::Editor::DebugEditorManager::BuildDefaultDockLayout(dockspaceId);
		defaultDockLayoutBuilt_ = true;
	}

#endif // _DEBUG



}

void ImGuiManager::End()
{
#ifdef _DEBUG

	ImGui::Render();

#endif // _DEBUG


}

void ImGuiManager::Draw()
{
#ifdef _DEBUG

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

	//デスクリプタヒープの配列をセットする
	ID3D12DescriptorHeap* ppHeaps[] = { srvHeap_.Get() };
	commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	//描画コマンドを発行
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif // _DEBUG


}

}
