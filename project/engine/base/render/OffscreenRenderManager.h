#pragma once
#include "Vector4.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>

enum class PostEffectType {
	Fullscreen,
	Grayscale,
	Vignette,
	BoxFilter,
	LuminanceOutline,
	RadialBlur,

};

/// @brief シーン描画をオフスクリーンに集約し、ポストエフェクト付きで合成するクラス
/// @details レンダーターゲットの生成、状態遷移、フルスクリーン描画を担当する。
namespace Engine::Base {

class DirectXCommon;
class GraphicsPipeline;
class SrvManager;

class OffscreenRenderManager
{
public:
	OffscreenRenderManager();
	~OffscreenRenderManager();
	static OffscreenRenderManager* GetInstance() { return instance_; }

	/// @brief オフスクリーン描画に必要なリソースを初期化する
	/// @param dxCommon DirectX共通管理
	/// @param srvManager SRV管理
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager);
	void Finalize();
	/// @brief オフスクリーン描画の開始処理を行う
	/// @param なし
	/// @return なし
	void Begin();
	void BindRenderTarget();
	/// @brief オフスクリーン描画の終了処理を行う
	/// @param なし
	/// @return なし
	void End();
	
	/// @brief オフスクリーン結果を最終画面へ描画する
	/// @param なし
	/// @return なし
	void Draw();
	
	/// @brief レンダーターゲットテクスチャを生成する
	/// @param width 横幅
	/// @param height 縦幅
	/// @param format テクスチャフォーマット
	/// @param clearColor クリアカラー
	/// @return 生成したリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTargetTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, const Vector4& clearColor);

	void SetPostEffectType(PostEffectType type) {
		currentEffectType_ = type;
	}
	void SetScenePostEffectType(PostEffectType type);

	void DrawImGui(bool* open = nullptr);
	void CreateImGuiSceneTextureSrv(
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
	D3D12_GPU_DESCRIPTOR_HANDLE GetImGuiSceneTextureHandle() const { return imGuiSceneTextureGpuHandle_; }
	bool HasImGuiSceneTexture() const { return imGuiSceneTextureReady_; }
private:
	std::shared_ptr<Engine::Base::DirectXCommon> GetDirectXCommon() const;
	static OffscreenRenderManager* instance_;

	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	//SRVManagerのポインタ
	Engine::Base::SrvManager* srvManager_ = nullptr;
	//レンダーテクスチャ
	DirectX::TexMetadata renderTargetMetadata_;
	Microsoft::WRL::ComPtr<ID3D12Resource> renderTargetTextureResource;//レンダーテクスチャ
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetTextureHandle;//レンダーテクスチャのハンドル
	inline static const Vector4 kClearColor = { 0.55f,0.68f,0.92f,1.0f };
	uint32_t srvIndex = UINT32_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE imGuiSceneTextureCpuHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE imGuiSceneTextureGpuHandle_{};
	bool imGuiSceneTextureReady_ = false;

	PostEffectType currentEffectType_ = PostEffectType::Fullscreen;
	PostEffectType sceneEffectType_ = PostEffectType::Fullscreen;
	bool autoSceneEffectEnabled_ = true;
	std::unique_ptr<GraphicsPipeline> graphicsPipeline_;

};

}

