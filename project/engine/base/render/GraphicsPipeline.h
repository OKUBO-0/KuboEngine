#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "PipelineStateBuilder.h"
#include <map>
#include <memory>
#include <string>

enum class PostEffectType;
/// @brief 描画種別ごとのルートシグネチャとPSOを生成・保持するクラス
/// @details 3D、スプライト、パーティクル、ライン、スキニング、ポストエフェクトを扱う。
namespace Engine::Base {

class DirectXCommon;

class GraphicsPipeline
{
public:
	enum class PipelineInputLayout {
		Standard,
		PositionTexcoord,
		Skinning,
		Line,
		Empty,
	};

	enum class PipelineBlendMode {
		Alpha,
		Additive,
		Disabled,
	};

	enum class PipelineDepthMode {
		ReadWrite,
		ReadOnly,
		Disabled,
	};

	struct PipelineResourceSet {
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	};

	/// @brief PSO 作成に必要な設定一式
	/// @details 新しい描画種別は専用 CreateXXX を追加せず、この構造体を渡して登録できる。
	struct PipelineCreateDesc {
		const char* key = nullptr;
		ID3D12RootSignature* rootSignature = nullptr;
		const wchar_t* vertexShaderPath = nullptr;
		const wchar_t* pixelShaderPath = nullptr;
		PipelineInputLayout inputLayout = PipelineInputLayout::Standard;
		PipelineBlendMode blendMode = PipelineBlendMode::Alpha;
		PipelineDepthMode depthMode = PipelineDepthMode::ReadWrite;
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
		UINT renderTargetCount = 1;
		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		int depthBias = 0;
		float slopeScaledDepthBias = 0.0f;
		float depthBiasClamp = 0.0f;
		const char* failureContext = "ID3D12Device::CreateGraphicsPipelineState";
	};

	/// @brief DirectX 共通参照を保持する
	/// @param dxCommon DirectX 共通管理
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon);

	/// @brief 設定構造体から PSO を作成して key 登録する
	/// @param desc ルートシグネチャ、シェーダ、入力レイアウト、ブレンド、深度などの作成設定
	/// @return 生成した PSO。失敗時は例外を投げる
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipeline(
		const PipelineCreateDesc& desc);

	/// @brief 3Dオブジェクト用PSOを生成する互換ラッパー
	void Create();
	/// @brief 3Dオブジェクト用ルートシグネチャを生成する
	void RootSignatureCreate();
	void CreateObjectInstancing();
	void CreateObjectInstancingShadowMap();
	void RootSignatureObjectInstancingCreate();

	/// @brief パーティクル用PSOを生成する互換ラッパー
	void CreateParticle();
	/// @brief パーティクル用ルートシグネチャを生成する
	void RootSignatureParticleCreate();

	/// @brief スプライト用PSOを生成する互換ラッパー
	void CreateSprite();
	/// @brief スプライト用ルートシグネチャを生成する
	void RootSignatureSpriteCreate();

	void CreateCopyImage(PostEffectType type, const std::wstring& psFilename);
	void CreateAllPostEffects();
	void RootSignatureCopyImageCreate();
	
	/// @brief ライン用PSOを生成する互換ラッパー
	void CreateLine();
	/// @brief ライン用ルートシグネチャを生成する
	void RootSignatureLineCreate();

	/// @brief スキニング用PSOを生成する互換ラッパー
	void CreateSkinning();//スキニング用
	/// @brief スキニング用ルートシグネチャを生成する
	void RootSignatureSkinningCreate();//スキニング用
	void CreateSkinningInstancing();
	void CreateSkinningInstancingShadowMap();
	void RootSignatureSkinningInstancingCreate();
	void CreateShadowMap();
	void CreateSkinningShadowMap();
	void RootSignatureShadowMapCreate();

	/// @brief スカイボックス用PSOを生成する互換ラッパー
	void CreateSkybox();
	/// @brief スカイボックス用ルートシグネチャを生成する
	void RootSignatureSkyboxCreate();

	/// @brief 任意キーでパイプライン資源を登録する
	/// @details FW 側の共通クラスでも、描画種別を getter 追加だけに閉じず拡張できるようにする。
	void RegisterPipeline(
		const std::string& key,
		const Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
		const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState);

	/// @brief 任意の PSO 設定を生成して登録する
	/// @details 新しい描画種別を追加するとき、専用 getter / 専用 member を増やさず key で扱える。
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateAndRegisterPipelineState(
		const std::string& key,
		const Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
		const GraphicsPipelineStateRequest& request);

	/// @brief 登録済みパイプライン資源を検索する
	const PipelineResourceSet* FindPipeline(const std::string& key) const;

	/// @brief ルートシグネチャを ComPtr で取得し、呼び出し側が保持中に解放されないようにする
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureHandle(const std::string& key) const;

	/// @brief PSO を ComPtr で取得し、呼び出し側が保持中に解放されないようにする
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineStateHandle(const std::string& key) const;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureObjectHandle() const { return rootSignature; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateObjectHandle() const { return graphicsPipelineState; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureObjectInstancingHandle() const { return rootSignatureObjectInstancing; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateObjectInstancingHandle() const { return graphicsPipelineStateObjectInstancing; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateObjectInstancingShadowMapHandle() const { return graphicsPipelineStateObjectInstancingShadowMap; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureParticleHandle() const { return rootSignatureParticle; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateParticleHandle() const { return graphicsPipelineStateParticle; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureSpriteHandle() const { return rootSignatureSprite; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSpriteHandle() const { return graphicsPipelineStateSprite; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureCopyImageHandle() const { return rootSignatureCopyImage; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureLineHandle() const { return rootSignatureLine; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateLineHandle() const { return graphicsPipelineStateLine; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureSkinningHandle() const { return rootSignatureSkinning; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSkinningHandle() const { return graphicsPipelineStateSkinning; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureSkinningInstancingHandle() const { return rootSignatureSkinningInstancing; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSkinningInstancingHandle() const { return graphicsPipelineStateSkinningInstancing; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSkinningInstancingShadowMapHandle() const { return graphicsPipelineStateSkinningInstancingShadowMap; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureShadowMapHandle() const { return rootSignatureShadowMap; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateShadowMapHandle() const { return graphicsPipelineStateShadowMap; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSkinningShadowMapHandle() const { return graphicsPipelineStateSkinningShadowMap; }
	Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignatureSkyboxHandle() const { return rootSignatureSkybox; }
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateSkyboxHandle() const { return graphicsPipelineStateSkybox; }

	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateCopyImageHandle(PostEffectType type) const;

private:
	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureObjectInstancing = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateObjectInstancing = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateObjectInstancingShadowMap = nullptr;

	//スキニング用のルートシグネチャとパイプラインステートオブジェクト
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSkinning = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkinning = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSkinningInstancing = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkinningInstancing = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkinningInstancingShadowMap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureShadowMap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateShadowMap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkinningShadowMap = nullptr;

	//パーティクル用のルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureParticle = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateParticle = nullptr;


	//スプライト用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSprite = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSprite = nullptr;


	//コピーイメージ用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureCopyImage = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateCopyImage = nullptr;
	std::map<PostEffectType, Microsoft::WRL::ComPtr<ID3D12PipelineState>> copyImagePipelines_; 
	std::map<std::string, PipelineResourceSet> pipelineRegistry_;


	//ライン用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureLine = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateLine = nullptr;

	//Skybox用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSkybox = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkybox = nullptr;



};

}

