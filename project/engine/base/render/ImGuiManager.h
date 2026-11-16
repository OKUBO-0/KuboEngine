#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include "ImGuiDescriptorSlots.h"
#include <filesystem>
#include <wrl.h>

struct ImGui_ImplDX12_InitInfo;
struct ImGuiContext;
struct ImPlotContext;
namespace ax::NodeEditor {
struct EditorContext;
}

/// @brief ImGui の初期化とフレーム進行を管理するクラス
/// @details DirectX12 と Win32 に ImGui を接続し、受付開始、終了、描画を担当する。
namespace Engine::Base {

class DirectXCommon;
class WinApp;
struct TextureLease;

class ImGuiManager
{
public:
	ImGuiManager() = default;
	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;
	// Finalize on the initializing host thread while graphics/window owners live.
	/// @brief ImGui を使用可能な状態に初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param winApp ウィンドウ管理クラス
	/// @return なし
	void Initialize(std::shared_ptr<DirectXCommon> dxCommon, Engine::Base::WinApp* winApp, const std::filesystem::path& projectRoot = {});

	/// @brief ImGui の使用資源を解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief ImGui のフレーム受付を開始する
	/// @param なし
	/// @return なし
	void Begin();
	void RequestDefaultDockLayout() noexcept { resetDockLayout_ = true; }
	void RequestDockWorkspace(bool diagnostics) noexcept { diagnosticsDockLayout_ = diagnostics; resetDockLayout_ = true; }
	
	/// @brief ImGui のフレーム受付を終了する
	/// @param なし
	/// @return なし
	void End();

	/// @brief 生成済み ImGui 描画データを画面へ描く
	/// @param なし
	/// @return なし
	void Draw();
	uint32_t GetInvalidDescriptorReleaseCount() const noexcept { return invalidDescriptorReleaseCount_; }
	// Host thread, before Begin: source leases/resources stay owned by caller.
	// Returned handles belong to the ImGui heap, not SrvManager's heap.
	uint64_t CreateExternalTextureSrv(const TextureLease& lease);
	uint64_t CreateExternalTextureSrv(ID3D12Resource* image);
	// Caller has drained submitted/unsubmitted GPU work before releasing/reusing.
	void ReleaseExternalTextureAfterGpuIdle(uint64_t texture);

private:
	std::filesystem::path layoutPath_;
	bool layoutPersistenceEnabled_ = false;
	void SaveLayout();
	void RequireReady() const;
	void RecordInvalidDescriptorRelease() noexcept;
	std::shared_ptr<DirectXCommon> GetDirectXCommon() const;
	static void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
	static void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>srvHeap_;
	uint32_t srvDescriptorSize_ = 0;
	ImGuiDescriptorSlots descriptorSlots_;
	std::array<bool, ImGuiDescriptorSlots::kCapacity> externalSlots_{};
	uint32_t AllocateExternalTextureSlot();
	uint32_t invalidDescriptorReleaseCount_ = 0;
	ImGuiContext* imGuiContext_ = nullptr;
	uint32_t ownerThreadId_ = 0;
	bool ready_ = false, finalizing_ = false;
	ImPlotContext* imPlotContext_ = nullptr;
	ax::NodeEditor::EditorContext* nodeEditorContext_ = nullptr;
	bool defaultDockLayoutBuilt_ = false;
	bool resetDockLayout_ = false;
	bool diagnosticsDockLayout_ = false;
	unsigned int windowDpi_ = 0;

	std::weak_ptr<DirectXCommon> dxCommon_;
	Engine::Base::WinApp* winApp_ = nullptr;
};

}

