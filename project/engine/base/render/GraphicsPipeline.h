#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "PipelineStateBuilder.h"
#include <map>
#include <string>

enum class PostEffectType;
/// @brief 描画種別ごとのルートシグネチャとPSOを生成・保持するクラス
/// @details 3D、スプライト、パーティクル、ライン、スキニング、ポストエフェクトを扱う。
namespace Engine::Base {

class DirectXCommon;

class GraphicsPipeline
{
public:
	struct PipelineResourceSet {
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	};

	/// @brief DirectX 共通参照を保持する
	/// @param dxCommon DirectX 共通管理
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon);

	/// @brief 3Dオブジェクト用PSOを生成する
	void Create();
	/// @brief 3Dオブジェクト用ルートシグネチャを生成する
	void RootSignatureCreate();

	/// @brief パーティクル用PSOを生成する
	void CreateParticle();
	/// @brief パーティクル用ルートシグネチャを生成する
	void RootSignatureParticleCreate();

	/// @brief スプライト用PSOを生成する
	void CreateSprite();
	/// @brief スプライト用ルートシグネチャを生成する
	void RootSignatureSpriteCreate();

	void CreateCopyImage(PostEffectType type, const std::wstring& psFilename);
	void CreateAllPostEffects();
	void RootSignatureCopyImageCreate();
	
	/// @brief ライン用PSOを生成する
	void CreateLine();
	/// @brief ライン用ルートシグネチャを生成する
	void RootSignatureLineCreate();

	/// @brief スキニング用PSOを生成する
	void CreateSkinning();//スキニング用
	/// @brief スキニング用ルートシグネチャを生成する
	void RootSignatureSkinningCreate();//スキニング用
	void CreateShadowMap();
	void RootSignatureShadowMapCreate();

	/// @brief スカイボックス用PSOを生成する
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



	//ゲッター
	ID3D12RootSignature* GetRootSignature()const { return rootSignature.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineState()const { return graphicsPipelineState.Get(); }
	//パーティクル用のPSO
	ID3D12RootSignature* GetRootSignatureParticle()const { return rootSignatureParticle.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateParticle()const { return graphicsPipelineStateParticle.Get(); }

	//スプライト用のPSO
	ID3D12RootSignature* GetRootSignatureSprite()const { return rootSignatureSprite.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateSprite()const { return graphicsPipelineStateSprite.Get(); }

	//コピーイメージ用のPSO
	ID3D12RootSignature* GetRootSignatureCopyImage()const { return rootSignatureCopyImage.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateCopyImage(PostEffectType type);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetGraphicsPipelineStateCopyImageHandle(PostEffectType type) const;

	//ライン用のPSO
	ID3D12RootSignature* GetRootSignatureLine()const { return rootSignatureLine.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateLine()const { return graphicsPipelineStateLine.Get(); }

	//スキニング用のPSO
	ID3D12RootSignature* GetRootSignatureSkinning()const { return rootSignatureSkinning.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateSkinning()const { return graphicsPipelineStateSkinning.Get(); }
	ID3D12RootSignature* GetRootSignatureShadowMap()const { return rootSignatureShadowMap.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateShadowMap()const { return graphicsPipelineStateShadowMap.Get(); }

	//Skybox用のPSO
	ID3D12RootSignature* GetRootSignatureSkybox()const { return rootSignatureSkybox.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateSkybox()const { return graphicsPipelineStateSkybox.Get(); }

private:
	Engine::Base::DirectXCommon* dxCommon_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;

	//スキニング用のルートシグネチャとパイプラインステートオブジェクト
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSkinning = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkinning = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureShadowMap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateShadowMap = nullptr;

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

