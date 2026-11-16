#pragma once
#include "Vector4.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include "ViewExtent.h"

#include "PostEffectType.h"

/// @brief シーン描画をオフスクリーンに集約し、ポストエフェクト付きで合成するクラス
/// @details レンダーターゲットの生成、状態遷移、フルスクリーン描画を担当する。
namespace Engine::Base {

class DirectXCommon;
class GraphicsPipeline;
class SrvManager;

struct ViewAppearance {
    Vector4 background{.55f,.68f,.92f,1};
    float exposure=0;
    PostEffectType effect=PostEffectType::Fullscreen;
};

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
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager, ViewExtent gameExtent = {});
    void SetViewAppearance(const ViewAppearance& appearance) noexcept {appearance_=appearance;}
	ViewExtent GetGameOutputExtent() const { return gameOutputExtent_; }
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
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTargetTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, const Vector4& clearColor, bool optimizeClear = true);

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
	void InitializeEditorView();
	void ResizeEditorView(ViewExtent extent);
	ViewExtent GetEditorViewExtent() const { return editorViewExtent_; }
	void BeginEditorView();
	void BindEditorView();
	void EndEditorView(bool applyPost=true);
#ifndef KUBO_RUNTIME_ONLY
	void DrawEditorGrid(const float (&viewProjection)[4][4]);
	void DrawEditorPreview(D3D12_CPU_DESCRIPTOR_HANDLE target, ViewExtent extent);
#endif
	bool HasEditorView() const { return editorViewSrv_ != UINT32_MAX; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetEditorViewHandle() const { return imGuiEditorViewGpu_; }
	void SetImGuiEditorViewSlot(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
	// Non-owning capture/debug access; valid until manager finalization.
	ID3D12Resource* GetViewRenderTarget(bool editor) const { return editor ? editorViewColor_.Get() : renderTargetTextureResource.Get(); }
	ID3D12Resource* GetViewDepthTarget(bool editor) const { return editor ? editorViewDepth_.Get() : gameDepth_.Get(); }
private:
	std::shared_ptr<Engine::Base::DirectXCommon> GetDirectXCommon() const;
	static OffscreenRenderManager* instance_;

	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	//SRVManagerのポインタ
	Engine::Base::SrvManager* srvManager_ = nullptr;
	//レンダーテクスチャ
	// These targets are ordinary 2D textures, never inherited CubeMap flags
	// from uninitialized storage (the out-of-line default ctor does not zero it).
	DirectX::TexMetadata renderTargetMetadata_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> renderTargetTextureResource;//レンダーテクスチャ
	ViewExtent gameOutputExtent_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> gameDepth_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> gameDsv_;
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetTextureHandle;//レンダーテクスチャのハンドル
    struct PostTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv;
    };
    void FinishView(ID3D12Resource* source,uint32_t sourceSrv,ViewExtent extent,PostTarget& target);
    void ResetPostTarget(PostTarget& target);
    void DrawPostEffect(uint32_t sourceSrv,PostEffectType effect,float exposureScale);
    PostTarget gamePost_,editorPost_;
    ViewAppearance appearance_;
	uint32_t srvIndex = UINT32_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE imGuiSceneTextureCpuHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE imGuiSceneTextureGpuHandle_{};
	bool imGuiSceneTextureReady_ = false;

	PostEffectType currentEffectType_ = PostEffectType::Fullscreen;
	PostEffectType sceneEffectType_ = PostEffectType::Fullscreen;
	bool autoSceneEffectEnabled_ = true;
	std::unique_ptr<GraphicsPipeline> graphicsPipeline_;
	Microsoft::WRL::ComPtr<ID3D12Resource> editorViewColor_, editorViewDepth_;
	ViewExtent editorViewExtent_{};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> editorViewRtv_, editorViewDsv_;
	uint32_t editorViewSrv_ = UINT32_MAX;
	D3D12_GPU_DESCRIPTOR_HANDLE editorViewGpu_{};
	D3D12_CPU_DESCRIPTOR_HANDLE imGuiEditorViewCpu_{};
	D3D12_GPU_DESCRIPTOR_HANDLE imGuiEditorViewGpu_{};

};

}

