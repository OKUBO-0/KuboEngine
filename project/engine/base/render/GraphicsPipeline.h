#pragma once
#include <d3d12.h>
#include <wrl.h>
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
	/// @brief DirectX 共通参照を保持する
	/// @param dxCommon DirectX 共通管理
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon);

	/// @brief 3Dオブジェクト用PSOを生成する
	void Create();//3dオブジェクト用
	/// @brief 3Dオブジェクト用ルートシグネチャを生成する
	void RootSignatureCreate();//3dオブジェクト用

	/// @brief パーティクル用PSOを生成する
	void CreateParticle();//パーティクル用
	/// @brief パーティクル用ルートシグネチャを生成する
	void RootSignatureParticleCreate();//パーティクル用

	/// @brief スプライト用PSOを生成する
	void CreateSprite();//スプライト用
	/// @brief スプライト用ルートシグネチャを生成する
	void RootSignatureSpriteCreate();//スプライト用

	void CreateCopyImage(PostEffectType type, const std::wstring& psFilename); // ← 従来通りの単一バージョン
	void CreateAllPostEffects(); // ← 新：複数ポストエフェクト用
	void RootSignatureCopyImageCreate();
	
	/// @brief ライン用PSOを生成する
	void CreateLine();//ライン用
	/// @brief ライン用ルートシグネチャを生成する
	void RootSignatureLineCreate();//ライン用

	/// @brief スキニング用PSOを生成する
	void CreateSkinning();//スキニング用
	/// @brief スキニング用ルートシグネチャを生成する
	void RootSignatureSkinningCreate();//スキニング用

	/// @brief スカイボックス用PSOを生成する
	void CreateSkybox();//Skybox用
	/// @brief スカイボックス用ルートシグネチャを生成する
	void RootSignatureSkyboxCreate();//Skybox用



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

	//ライン用のPSO
	ID3D12RootSignature* GetRootSignatureLine()const { return rootSignatureLine.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateLine()const { return graphicsPipelineStateLine.Get(); }

	//スキニング用のPSO
	ID3D12RootSignature* GetRootSignatureSkinning()const { return rootSignatureSkinning.Get(); }
	ID3D12PipelineState* GetGraphicsPipelineStateSkinning()const { return graphicsPipelineStateSkinning.Get(); }

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


	//ライン用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureLine = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateLine = nullptr;

	//Skybox用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureSkybox = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateSkybox = nullptr;



};

}

