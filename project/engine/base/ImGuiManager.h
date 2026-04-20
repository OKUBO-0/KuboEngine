#pragma once
#include "DirectXCommon.h"
#include "WinApp.h"

/// @brief ImGui の初期化とフレーム進行を管理するクラス
/// @details DirectX12 と Win32 に ImGui を接続し、受付開始、終了、描画を担当する。
namespace Engine::Base {

class ImGuiManager
{
public:
	/// @brief ImGui を使用可能な状態に初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param winapp ウィンドウ管理クラス
	/// @return なし
	void Initialize(DirectXCommon* dxCommon, Engine::Base::WinApp* winapp);

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
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>srvHeap_;

	DirectXCommon* dxCommon_ = nullptr;
	Engine::Base::WinApp* winapp_ = nullptr;
};

}

