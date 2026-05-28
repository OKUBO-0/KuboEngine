#include "ImGuiManager.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

namespace Engine::Base {

void ImGuiManager::Initialize(DirectXCommon* dxCommon, Engine::Base::WinApp* winapp)
{
#ifdef _DEBUG

	dxCommon_ = dxCommon;
	winapp_ = winapp;

	//imguiのコンテキストを生成
	ImGui::CreateContext();
	//imguiのスタイルを設定
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(winapp_->GetHwnd());
	//デスクリプタヒープ設定
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//デスクリプターフープ生成
	HRESULT hr = dxCommon_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
	assert(SUCCEEDED(hr));

	ImGui_ImplDX12_Init(
		dxCommon_->GetDevice(),
		static_cast<int>(dxCommon_->GetBackBufferCount()),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvHeap_.Get(),
		srvHeap_->GetCPUDescriptorHandleForHeapStart(),
		srvHeap_->GetGPUDescriptorHandleForHeapStart()
	);
#endif // _DEBUG


}

void ImGuiManager::Finalize()
{
#ifdef _DEBUG

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
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
