#pragma once
#include <d3d12.h>
#include <cstdint>
#include <vector>
#include <wrl.h>

struct ImGui_ImplDX12_InitInfo;
struct ImPlotContext;
namespace ax::NodeEditor {
struct EditorContext;
}

/// @brief ImGui の初期化とフレーム進行を管理するクラス
/// @details DirectX12 と Win32 に ImGui を接続し、受付開始、終了、描画を担当する。
namespace Engine::Base {

class DirectXCommon;
class WinApp;

class ImGuiManager
{
public:
	/// @brief ImGui を使用可能な状態に初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param winApp ウィンドウ管理クラス
	/// @return なし
	void Initialize(DirectXCommon* dxCommon, Engine::Base::WinApp* winApp);

	/// @brief ImGui の使用資源を解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief ImGui のフレーム受付を開始する
	/// @param なし
	/// @return なし
	void Begin();
	
	/// @brief ImGui のフレーム受付を終了する
	/// @param なし
	/// @return なし
	void End();

	/// @brief 生成済み ImGui 描画データを画面へ描く
	/// @param なし
	/// @return なし
	void Draw();

private:
	static void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
	static void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>srvHeap_;
	uint32_t srvDescriptorSize_ = 0;
	uint32_t nextSrvDescriptorIndex_ = 0;
	std::vector<uint32_t> freeSrvDescriptorIndices_;
	ImPlotContext* imPlotContext_ = nullptr;
	ax::NodeEditor::EditorContext* nodeEditorContext_ = nullptr;
	bool defaultDockLayoutBuilt_ = false;

	DirectXCommon* dxCommon_ = nullptr;
	Engine::Base::WinApp* winApp_ = nullptr;
};

}

