#pragma once
#include "RenderingData.h"
#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct aiAnimation;
struct aiMesh;
struct aiNode;
struct aiNodeAnim;
struct aiScene;

namespace Engine::Graphics3D {

class ModelCommon;

// 1頂点あたりの最大ボーン影響数
const uint32_t kNumMaxInfluence = 4;

struct ModelLoadDiagnostics {
    uint32_t skippedNonTriangleFaceCount = 0;
    uint32_t meshFallbackCount = 0;
};

struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights;      // 各ボーンの重み
    std::array<int32_t, kNumMaxInfluence> jointIndices; // 各ボーンのインデックス
};

struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix;              // スケルトン空間行列（位置用）
    Matrix4x4 skeletonSpaceInverseTransposeMatrix; // 法線用行列
};

struct SkinCluster {
    // 各ジョイントの逆バインドポーズ行列
    std::vector<Matrix4x4> inverseBindPoseMatrices;

    // 頂点影響情報（ボーンと重み）
    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView;
    std::span<VertexInfluence> mappedInfluence;

    // ボーン行列（palette）
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    std::span<WellForGPU> mappedPalette;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle;
};

/// @brief 3D モデルの描画データとアニメーション資産を保持するクラス
/// @details 頂点・インデックス・マテリアル・スケルトン・スキンクラスターを生成し、
///          モデル読込と描画に必要な GPU リソースを管理する。
class Model
{
public:
    /// @brief モデル描画に必要なデータを初期化する
    /// @param modelCommon モデル共通描画設定
    /// @param directorypath モデルファイルが存在するディレクトリ
    /// @param filename 読み込むモデルファイル名
    /// @return なし
    void Initialize(ModelCommon* modelCommon, const std::string& directorypath, const std::string& filename);
    static ModelLoadDiagnostics GetLoadDiagnostics();

    /// @brief モデルを描画する
    /// @param なし
    /// @return なし
    void Draw();

    /// @brief Assimp ノードを再帰的に読み込んで内部ノードへ変換する
    /// @param node 読み込み対象の Assimp ノード
    /// @return 変換後のノード情報
    Node ReadNode(aiNode* node);

    /// @brief ルートノードからスケルトンを生成する
    /// @param rootNode スケルトン生成の起点となるノード
    /// @return 生成したスケルトン
    Skeleton CreateSkeleton(const Node& rootNode);

    /// @brief ノードをジョイント配列へ追加する
    /// @param node 追加対象のノード
    /// @param parent 親ジョイントのインデックス
    /// @param joints 追加先のジョイント配列
    /// @return 生成したジョイントのインデックス
    int32_t CreateJoint(const Node& node, std::optional<int32_t> parent, std::vector<Joint>& joints);

    // 各種データ取得
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const { return vertexBufferView; }
    const ModelData& GetModelData() const { return modelData; }
    float GetLocalBoundingRadius() const { return modelData.localBoundingRadius; }
    bool HasBounds() const { return modelData.hasBounds; }
    Animation& GetAnimation() { return animation; }
    Skeleton& GetSkeleton() { return skeleton; }
    SkinCluster& GetSkinCluster() { return skinCluster; }

    // マテリアル設定
    void SetEnableLighting(bool enable) { materialData->enableLighting = enable; }
    void SetColor(const Vector4& color) { materialData->color = color; }

    /// @brief マテリアルテンプレートファイルを読み込む
    /// @param directorypath マテリアルファイルのディレクトリ
    /// @param filename 読み込むファイル名
    /// @return 読み込んだマテリアルデータ
    MaterialData LoadMaterialTemplateFile(const std::string& directorypath, const std::string& filename);

    /// @brief モデルファイルを読み込んで描画データへ変換する
    /// @param directoryPath モデルファイルのディレクトリ
    /// @param filename 読み込むファイル名
    /// @return 読み込んだモデルデータ
    ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);

    /// @brief アニメーションファイルを読み込む
    /// @param directoryPath アニメーションファイルのディレクトリ
    /// @param filename 読み込むファイル名
    /// @return 読み込んだアニメーションデータ
    Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename);

    /// @brief スキニング用の GPU リソースを生成する
    /// @param なし
    /// @return 生成したスキンクラスター
    SkinCluster CreateSkinCluster();

private:
    /// @brief モデル読込後のランタイム資源を構築する
    /// @param directorypath モデルディレクトリ
    /// @param filename モデルファイル名
    /// @return なし
    void LoadRuntimeAssets(const std::string& directorypath, const std::string& filename);

    /// @brief 頂点バッファを生成して頂点データを転送する
    /// @param なし
    /// @return なし
    void CreateVertexBuffer();

    /// @brief インデックスバッファを生成してインデックスデータを転送する
    /// @param なし
    /// @return なし
    void CreateIndexBuffer();

    /// @brief マテリアルバッファを生成して初期値を書き込む
    /// @param なし
    /// @return なし
    void CreateMaterialBuffer();

    /// @brief モデルに対応するテクスチャを読み込む
    /// @param なし
    /// @return なし
    void LoadMaterialTexture();

    /// @brief メッシュ頂点データを内部形式へ変換する
    /// @param mesh Assimp メッシュ
    /// @param modelData 書き込み先モデルデータ
    /// @return なし
    void LoadVerticesFromMesh(aiMesh* mesh, ModelData& modelData);
    static void CalculateBounds(ModelData& modelData);

    /// @brief メッシュインデックスデータを内部形式へ変換する
    /// @param mesh Assimp メッシュ
    /// @param modelData 書き込み先モデルデータ
    /// @return なし
    void LoadIndicesFromMesh(aiMesh* mesh, ModelData& modelData);

    /// @brief メッシュのボーン情報をスキンクラスターデータへ変換する
    /// @param mesh Assimp メッシュ
    /// @param modelData 書き込み先モデルデータ
    /// @return なし
    void LoadSkinClusterDataFromMesh(aiMesh* mesh, ModelData& modelData);

    /// @brief シーン内マテリアルから代表テクスチャを抽出する
    /// @param scene Assimp シーン
    /// @param directoryPath モデルディレクトリ
    /// @param modelData 書き込み先モデルデータ
    /// @return なし
    void LoadMaterialFromScene(const aiScene* scene, const std::string& directoryPath, ModelData& modelData);

    /// @brief Assimp アニメーションの全チャンネルを内部形式へ変換する
    /// @param animationAssimp 読み込み元アニメーション
    /// @param animation 書き込み先アニメーション
    /// @return なし
    void LoadAnimationChannels(const aiAnimation* animationAssimp, Animation& animation);

    /// @brief 位置キーフレーム列を読み込む
    /// @param nodeAnimationAssimp 読み込み元チャンネル
    /// @param ticksPerSecond 秒換算係数
    /// @param nodeAnimation 書き込み先ノードアニメーション
    /// @return なし
    void LoadTranslateKeys(const aiNodeAnim* nodeAnimationAssimp, float ticksPerSecond, NodeAnimation& nodeAnimation);

    /// @brief 回転キーフレーム列を読み込む
    /// @param nodeAnimationAssimp 読み込み元チャンネル
    /// @param ticksPerSecond 秒換算係数
    /// @param nodeAnimation 書き込み先ノードアニメーション
    /// @return なし
    void LoadRotateKeys(const aiNodeAnim* nodeAnimationAssimp, float ticksPerSecond, NodeAnimation& nodeAnimation);

    /// @brief スケールキーフレーム列を読み込む
    /// @param nodeAnimationAssimp 読み込み元チャンネル
    /// @param ticksPerSecond 秒換算係数
    /// @param nodeAnimation 書き込み先ノードアニメーション
    /// @return なし
    void LoadScaleKeys(const aiNodeAnim* nodeAnimationAssimp, float ticksPerSecond, NodeAnimation& nodeAnimation);

    /// @brief パレット用 GPU リソースを初期化する
    /// @param skinCluster 初期化対象
    /// @return なし
    void InitializePaletteResources(SkinCluster& skinCluster);

    /// @brief 頂点インフルエンス用 GPU リソースを初期化する
    /// @param skinCluster 初期化対象
    /// @return なし
    void InitializeInfluenceResources(SkinCluster& skinCluster);

    /// @brief 逆バインドポーズ行列配列を単位行列で初期化する
    /// @param skinCluster 初期化対象
    /// @return なし
    void InitializeInverseBindPoseMatrices(SkinCluster& skinCluster);

    /// @brief 読み込み済みウェイトをスケルトン順へ並べ替えて反映する
    /// @param skinCluster 反映先
    /// @return なし
    void ApplyJointWeightsToSkinCluster(SkinCluster& skinCluster);

    ModelCommon* modelCommon_ = nullptr; // 共通部

    ModelData modelData;   // モデルデータ
    Animation animation;   // アニメーションデータ
    Skeleton skeleton;     // スケルトン
    SkinCluster skinCluster; // スキンクラスター

    // GPUリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource; // 頂点バッファ
    VertexData* vertexData = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource; // マテリアルバッファ
    Material* materialData = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource; // インデックスバッファ
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	uint32_t* mappedIndex = nullptr;
};

}
