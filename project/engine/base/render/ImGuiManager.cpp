#include "ImGuiManager.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "HResult.h"
#include "DebugEditorManager.h"
#include "ImGuizmoManager.h"
#include "OffscreenRenderManager.h"
#include "WinApp.h"
#include "EngineResourcePath.h"
#include "engine/base/AssetPath.h"
#include "engine/base/ProjectPaths.h"
#include "engine/editor/EditorSettingsFile.h"
#include "Logger.h"
#include <cassert>
#include "IconsFontAwesome5.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <implot.h>
#include <imgui_node_editor.h>
#include <filesystem>
#include <exception>
#include <limits>
#include <stdexcept>

namespace Engine::Base {

namespace {

#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
void ApplyEditorStyle(float scale)
{
	ImGui::GetStyle() = ImGuiStyle{};
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
	Engine::Editor::DebugEditorManager::ApplyDarkEditorStyle();
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = scale;
}
#endif

constexpr uint32_t kImGuiSrvDescriptorCount = ImGuiDescriptorSlots::kCapacity;
constexpr uint32_t kImGuiSceneTextureSrvIndex = 0;
constexpr uint32_t kImGuiEditorViewSrvIndex = 1;

}

void ImGuiManager::AllocateSrvDescriptor(
	ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
	ImGuiManager* manager = static_cast<ImGuiManager*>(info->UserData);
	uint32_t index = 0;
	if (!manager->descriptorSlots_.Allocate(index)) throw std::length_error("ImGui descriptor heap exhausted");

	*outCpuHandle = manager->srvHeap_->GetCPUDescriptorHandleForHeapStart();
	outCpuHandle->ptr += static_cast<SIZE_T>(manager->srvDescriptorSize_) * index;
	*outGpuHandle = manager->srvHeap_->GetGPUDescriptorHandleForHeapStart();
	outGpuHandle->ptr += static_cast<UINT64>(manager->srvDescriptorSize_) * index;
}

void ImGuiManager::FreeSrvDescriptor(
	ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
	ImGuiManager* manager = static_cast<ImGuiManager*>(info->UserData);
	if (!manager->srvHeap_ || !manager->srvDescriptorSize_) { manager->RecordInvalidDescriptorRelease(); return; }
	const auto start = manager->srvHeap_->GetCPUDescriptorHandleForHeapStart();
	const auto gpuStart = manager->srvHeap_->GetGPUDescriptorHandleForHeapStart();
	if (cpuHandle.ptr < start.ptr) { manager->RecordInvalidDescriptorRelease(); return; }
	const auto offset = cpuHandle.ptr - start.ptr;
	const auto index = offset / manager->srvDescriptorSize_;
	if (offset % manager->srvDescriptorSize_ || index >= kImGuiSrvDescriptorCount ||
	    gpuHandle.ptr != gpuStart.ptr + static_cast<UINT64>(offset) ||
	    !manager->descriptorSlots_.Release(static_cast<uint32_t>(index))) manager->RecordInvalidDescriptorRelease();
}

void ImGuiManager::RecordInvalidDescriptorRelease() noexcept {
	if (invalidDescriptorReleaseCount_ != UINT32_MAX) ++invalidDescriptorReleaseCount_;
}

uint32_t ImGuiManager::AllocateExternalTextureSlot() {
	RequireReady();uint32_t index=0;
	if(!descriptorSlots_.Allocate(index))throw std::length_error("ImGui preview slots exhausted");
	externalSlots_[index]=true;return index;
}
uint64_t ImGuiManager::CreateExternalTextureSrv(const TextureLease& lease) {
	const auto index=AllocateExternalTextureSlot();
	auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(srvDescriptorSize_)*index;
	try {TextureManager::GetInstance()->CreateLeasedTexture2DView(lease,cpu);}
	catch(...){externalSlots_[index]=false;descriptorSlots_.Release(index);throw;}
	return srvHeap_->GetGPUDescriptorHandleForHeapStart().ptr+UINT64(srvDescriptorSize_)*index;
}
uint64_t ImGuiManager::CreateExternalTextureSrv(ID3D12Resource* image) {
	if(!image)throw std::invalid_argument("Missing thumbnail image");
	const auto shape=image->GetDesc();
	if(shape.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||shape.DepthOrArraySize!=1||shape.SampleDesc.Count!=1||shape.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
		throw std::invalid_argument("Invalid thumbnail image shape/format");
	const auto index=AllocateExternalTextureSlot();
	auto cpu=srvHeap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(srvDescriptorSize_)*index;
	D3D12_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;desc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;
	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(image,&desc,cpu);
	return srvHeap_->GetGPUDescriptorHandleForHeapStart().ptr+UINT64(srvDescriptorSize_)*index;
}
void ImGuiManager::ReleaseExternalTextureAfterGpuIdle(uint64_t texture) {
	RequireReady();const auto start=srvHeap_->GetGPUDescriptorHandleForHeapStart().ptr;
	if(texture<start||!srvDescriptorSize_)throw std::invalid_argument("Foreign ImGui preview descriptor");
	const auto offset=texture-start,index=offset/srvDescriptorSize_;
	if(offset%srvDescriptorSize_||index>=externalSlots_.size()||!externalSlots_[size_t(index)]||!descriptorSlots_.Release(uint32_t(index)))
		throw std::invalid_argument("Unowned ImGui preview descriptor");
	externalSlots_[size_t(index)]=false;
}

void ImGuiManager::RequireReady() const {
	if (!ready_ || finalizing_ || ownerThreadId_ != GetCurrentThreadId() ||
	    ImGui::GetCurrentContext() != imGuiContext_ || ImPlot::GetCurrentContext() != imPlotContext_ ||
	    ax::NodeEditor::GetCurrentEditor() != nodeEditorContext_)
		throw std::logic_error("ImGui frame requires the initialized host thread and owned current contexts");
}

std::shared_ptr<DirectXCommon> ImGuiManager::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	if (!dxCommon) throw std::logic_error("ImGui graphics owner expired");
	return dxCommon;
}

void ImGuiManager::Initialize(std::shared_ptr<DirectXCommon> dxCommon, Engine::Base::WinApp* winApp, const std::filesystem::path& projectRoot)
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)

	if (imGuiContext_ || ready_ || finalizing_) throw std::logic_error("ImGui manager already acquired contexts");
	if (ImGui::GetCurrentContext() || ImPlot::GetCurrentContext() || ax::NodeEditor::GetCurrentEditor())
		throw std::logic_error("ImGui manager cannot adopt a pre-existing context");
	if (!dxCommon || !winApp || !winApp->GetHwnd() || !dxCommon->GetDevice() || !dxCommon->GetCommandQueue())
		throw std::invalid_argument("ImGui requires initialized graphics and window owners");
	ownerThreadId_ = GetCurrentThreadId();
	dxCommon_ = dxCommon;
	winApp_ = winApp;
	descriptorSlots_.Reset();
	externalSlots_.fill(false);
	invalidDescriptorReleaseCount_ = 0;
	try {

	//imguiのコンテキストを生成
	imGuiContext_ = ImGui::CreateContext();
	ImGui::SetCurrentContext(imGuiContext_);
	ImGui::GetIO().IniFilename = nullptr;
	imPlotContext_ = ImPlot::CreateContext();
	ImPlot::SetCurrentContext(imPlotContext_);
	ax::NodeEditor::Config nodeEditorConfig{};
	// This editor is a runtime debug preview. Persisted camera values can become
	// invalid and make NodeEditor::End iterate over an enormous canvas.
	nodeEditorConfig.SettingsFile = nullptr;
	nodeEditorContext_ = ax::NodeEditor::CreateEditor(&nodeEditorConfig);
	ax::NodeEditor::SetCurrentEditor(nodeEditorContext_);
	ImGuiIO& io = ImGui::GetIO();
	const ProjectPaths paths(projectRoot);
	layoutPath_=paths.EditorLayout();
	Engine::Editor::DebugEditorManager::SetProjectRoot(paths.Root());
	io.IniFilename=nullptr; // We own native-path, bounded and atomic persistence.
	std::string layoutText,layoutError;
	layoutPersistenceEnabled_=false;
	if(Engine::Editor::EditorSettingsFile::Read(layoutPath_,paths.Root()/"imgui.ini",layoutText,layoutError)) {
		layoutPersistenceEnabled_=true;
		if(!layoutText.empty()) ImGui::LoadIniSettingsFromMemory(layoutText.data(),layoutText.size());
	} else Logger::Log("Editor layout load failed: "+layoutError,Logger::Level::Warning);
	Engine::Editor::DebugEditorManager::DiscardRetiredDockWindows();
	diagnosticsDockLayout_ = false;
	windowDpi_ = 0;
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
	const auto iconFont = Engine::Base::PathToUtf8(Engine::Base::ResolveEngineResource(
		"EditorResources/Fonts/fa-solid-900.ttf"));
	io.Fonts->AddFontFromFileTTF(
		iconFont.c_str(),
		16.0f,
		&iconConfig,
		iconRanges);
	if (!ImGui_ImplWin32_Init(winApp_->GetHwnd())) throw std::runtime_error("ImGui Win32 backend initialization failed");
	//デスクリプタヒープ設定
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = kImGuiSrvDescriptorCount;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//デスクリプターフープ生成
	HRESULT hr = GetDirectXCommon()->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
	ThrowIfFailed(
		hr,
		"ID3D12Device::CreateDescriptorHeap ImGui");

	srvDescriptorSize_ = GetDirectXCommon()->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// ウィンドウ名やDock構成の更新を確実に反映する。
	// ユーザーによるリサイズはDock比率を保ったままImGui側で追従する。
	defaultDockLayoutBuilt_ = false;
	resetDockLayout_ = false;

	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = GetDirectXCommon()->GetDevice();
	initInfo.CommandQueue = GetDirectXCommon()->GetCommandQueue();
	initInfo.NumFramesInFlight = static_cast<int>(GetDirectXCommon()->GetBackBufferCount());
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = srvHeap_.Get();
	initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
	initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;
	initInfo.UserData = this;
	if (!ImGui_ImplDX12_Init(&initInfo)) throw std::runtime_error("ImGui DX12 backend initialization failed");

	if (OffscreenRenderManager* offscreen = OffscreenRenderManager::GetInstance()) {
		D3D12_CPU_DESCRIPTOR_HANDLE sceneCpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
		sceneCpuHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * kImGuiSceneTextureSrvIndex;
		D3D12_GPU_DESCRIPTOR_HANDLE sceneGpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
		sceneGpuHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * kImGuiSceneTextureSrvIndex;
		offscreen->CreateImGuiSceneTextureSrv(sceneCpuHandle, sceneGpuHandle);
		auto editorCpuHandle=srvHeap_->GetCPUDescriptorHandleForHeapStart();
		auto editorGpuHandle=srvHeap_->GetGPUDescriptorHandleForHeapStart();
		editorCpuHandle.ptr+=static_cast<SIZE_T>(srvDescriptorSize_)*kImGuiEditorViewSrvIndex;
		editorGpuHandle.ptr+=static_cast<UINT64>(srvDescriptorSize_)*kImGuiEditorViewSrvIndex;
		offscreen->SetImGuiEditorViewSlot(editorCpuHandle,editorGpuHandle);
	}
	ready_ = true;
	} catch (...) {
		try { Finalize(); } catch (...) { /* Preserve the initialization diagnostic. */ }
		throw;
	}
#endif // _DEBUG


}

void ImGuiManager::Finalize()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if (finalizing_) return;
	if (ownerThreadId_ && ownerThreadId_ != GetCurrentThreadId()) throw std::logic_error("ImGui shutdown requires the initializing thread");
	for(const auto external:externalSlots_)if(external)throw std::logic_error("Retire external UI textures before ImGui shutdown");
	finalizing_ = true;
	struct FinalizingScope { bool& flag; ~FinalizingScope() { flag = false; } } scope{finalizing_};
	auto* previousGui = ImGui::GetCurrentContext();
	auto* previousPlot = ImPlot::GetCurrentContext();
	auto* previousEditor = ax::NodeEditor::GetCurrentEditor();
	struct RestoreContexts {
		ImGuiContext* gui; ImPlotContext* plot; ax::NodeEditor::EditorContext* editor;
		ImGuiContext*& ownedGui; ImPlotContext*& ownedPlot; ax::NodeEditor::EditorContext*& ownedEditor;
		bool guiWasOwned, plotWasOwned, editorWasOwned;
		~RestoreContexts() {
			ImGui::SetCurrentContext(guiWasOwned && ownedGui != gui ? nullptr : gui);
			ImPlot::SetCurrentContext(plotWasOwned && ownedPlot != plot ? nullptr : plot);
			ax::NodeEditor::SetCurrentEditor(editorWasOwned && ownedEditor != editor ? nullptr : editor);
		}
	} restore{previousGui, previousPlot, previousEditor, imGuiContext_, imPlotContext_, nodeEditorContext_,
	    previousGui == imGuiContext_, previousPlot == imPlotContext_, previousEditor == nodeEditorContext_};
	std::exception_ptr layoutFailure;
	if (imGuiContext_) {
		ImGui::SetCurrentContext(imGuiContext_);
		// IO publication, not Init return, tracks partial backend acquisition.
		if (ImGui::GetIO().BackendRendererUserData) GetDirectXCommon()->WaitForAllFrames();
		if (ready_) { try { SaveLayout(); } catch (...) { layoutFailure = std::current_exception(); } }
		ready_ = false;
		if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX12_Shutdown();
		if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
	}
	if (nodeEditorContext_) { ax::NodeEditor::DestroyEditor(nodeEditorContext_); nodeEditorContext_ = nullptr; }
	if (imPlotContext_) { ImPlot::DestroyContext(imPlotContext_); imPlotContext_ = nullptr; }
	if (imGuiContext_) { ImGui::DestroyContext(imGuiContext_); imGuiContext_ = nullptr; }

	srvHeap_.Reset();
	dxCommon_.reset();
	winApp_ = nullptr;
	layoutPath_.clear();
	layoutPersistenceEnabled_=false;
	descriptorSlots_.Reset();
	srvDescriptorSize_ = 0;
	defaultDockLayoutBuilt_ = false;
	ready_ = false;
	ownerThreadId_ = 0;
	if (layoutFailure) std::rethrow_exception(layoutFailure);
#endif // _DEBUG


}

void ImGuiManager::Begin()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	RequireReady();
	const auto currentDpi = winApp_->GetWindowDpi();
	if (windowDpi_ != currentDpi) {
		windowDpi_ = currentDpi;
		ApplyEditorStyle(static_cast<float>(windowDpi_) / USER_DEFAULT_SCREEN_DPI);
	}

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	Engine::Editor::ImGuizmoManager::BeginFrame();
	const auto* viewport=ImGui::GetMainViewport();
	// Match the bundled ImGui default ID, preserving existing layout migrations.
	char hostName[32];ImFormatString(hostName,IM_ARRAYSIZE(hostName),"WindowOverViewport_%08X",viewport->ID);
	const ImGuiID dockspaceId=ImHashStr("DockSpace",0,ImHashStr(hostName));
	if (!defaultDockLayoutBuilt_ || resetDockLayout_) {
		if (resetDockLayout_ || !ImGui::DockBuilderGetNode(dockspaceId))
			Engine::Editor::DebugEditorManager::BuildDefaultDockLayout(dockspaceId,diagnosticsDockLayout_);
		defaultDockLayoutBuilt_ = true;
		resetDockLayout_ = false;
	}
	ImGui::DockSpaceOverViewport(dockspaceId,viewport,ImGuiDockNodeFlags_None);

#endif // _DEBUG



}

void ImGuiManager::End()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	RequireReady();

	ImGui::Render();
	if(ImGui::GetIO().WantSaveIniSettings) { SaveLayout();ImGui::GetIO().WantSaveIniSettings=false; }

#endif // _DEBUG


}

void ImGuiManager::SaveLayout()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	if(layoutPath_.empty() || !layoutPersistenceEnabled_) return;
	size_t size=0;const char* text=ImGui::SaveIniSettingsToMemory(&size);std::string error;
	if(!Engine::Editor::EditorSettingsFile::Write(layoutPath_,std::string(text,size),error))
		Logger::Log("Editor layout save failed: "+error,Logger::Level::Warning);
#endif
}

void ImGuiManager::Draw()
{
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
	RequireReady();

	ID3D12GraphicsCommandList* commandList = GetDirectXCommon()->GetCommandList();

	//デスクリプタヒープの配列をセットする
	ID3D12DescriptorHeap* ppHeaps[] = { srvHeap_.Get() };
	commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	//描画コマンドを発行
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif // _DEBUG


}

}
