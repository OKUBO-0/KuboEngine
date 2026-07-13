#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "RenderingData.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "SkyBoxCommon.h"

/// @brief キューブマップを使ったスカイボックス描画クラス
/// @details 頂点・インデックス・マテリアル資源を保持し、
///          カメラに追従する背景用立方体を描画する。
namespace Engine::Skybox {

class SkyBox
{
public:
	SkyBox() = default;
	~SkyBox() ;

	/// @brief スカイボックス描画に必要な資源を初期化する
	/// @param textureFilePath 使用するキューブマップテクスチャのパス
	/// @return なし
	void Initialize(const std::string& textureFilePath);

	/// @brief 変換行列など描画前状態を更新する
	/// @param なし
	/// @return なし
	void Update();

	/// @brief スカイボックスを描画する
	/// @param なし
	/// @return なし
	void Draw();
	
	/// @brief ImGui から調整可能なデバッグ UI を描画する
	/// @param なし
	/// @return なし
	void DrawImGuiDebug();
	
	std::string GetTextureFilePath() const { return textureFilePath_; }

private:
	void InitializeGeometry();
	void InitializeFaceVertices(size_t startIndex, const std::array<Vector4, 4>& positions);
	void InitializeIndices();
	void InitializeVertexBuffer();
	void InitializeIndexBuffer();
	void InitializeTexture();
	void InitializeMaterial();

	Engine::Base::SrvManager* srvManager_ = nullptr;
	std::shared_ptr<Engine::Base::DirectXCommon> dxCommon_;

	//トランスフォーム
	TransformationMatrix transformationMatrixData_{};

	//indexバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource;
	//インデックスバッファビュー
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	std::vector<uint16_t> indices;
	


	std::vector<VertexData>vertices;
	//頂点リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	//VBV
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	Material materialData_{};
	std::string textureFilePath_;
	int textureIndex_ = 0;

	//SRT
	EulerTransform transform;
	Matrix4x4 worldMatrix;
	Matrix4x4 worldViewProjectionMatrix;

};

}

