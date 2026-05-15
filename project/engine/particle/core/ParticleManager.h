#pragma once
#include "IParticleBehavior.h"
#include "RenderingData.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::Base {
class DirectXCommon;
class GraphicsPipeline;
class SrvManager;
}

namespace Engine::Graphics3D {
class Model;
}

namespace Engine::Particle {

enum class VerticesType
{
	Ring,
	Cylinder,
	Quad,

};


struct Particle {

	EulerTransform transform;
	Vector3 Velocity;
	float lifetime;
	float currentTime;
	Vector4 color;
};

struct ParticleForGPU
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;

};

/// @brief パーティクルグループをまとめて更新・描画する管理クラス
/// @details グループ生成、挙動の差し替え、インスタンシング描画を担当する。
class ParticleManager
{
	struct ParticleGroup
	{
		MaterialData materialdata;
		std::string debugName;
		//particleの配列。Update 時に生存粒子を前詰めして compact する
		std::vector<Particle> particles;
		std::unique_ptr<IParticleBehavior> behavior;

		// インスタンシング用のSRVインデックス
		uint32_t srvIndex;
		// インスタンシング用のリソース
		Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource;
		// インスタンス数
		uint32_t instanceCount;
		uint32_t maxInstanceCount = 0;
		// インスタンスデータ
		ParticleForGPU* instanceData = nullptr;
		//頂点
		uint32_t vertexCount = 0;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
		//VBV
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
		//マテリアルにデータを書き込む	
		Material* materialData = nullptr;
	};
public:
	static constexpr uint32_t kDefaultMaxParticleInstanceCount = 1000;

	static ParticleManager* GetInstance();
	
private:
	// コンストラクタをプライベートにする
	ParticleManager() = default;
	~ParticleManager();
	// コピーコンストラクタと代入演算子を削除する
	ParticleManager(const ParticleManager&) = delete;
	ParticleManager& operator=(const ParticleManager&) = delete;


public:

	/// @brief パーティクル描画に必要なリソースを初期化する
	/// @param dxCommon DirectX共通管理
	/// @param srvManager SRV管理
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon,Engine::Base::SrvManager*srvManager);



	/// @brief パーティクル管理が保持するリソースを解放する
	/// @param なし
	/// @return なし
	void Finalize();
	/// @brief すべてのパーティクルを更新する
	/// @param なし
	/// @return なし
	void Update();
	/// @brief すべてのパーティクルを指定 deltaTime で更新する
	/// @param deltaTime 外部から渡されるフレーム経過秒
	/// @return なし
	void Update(float deltaTime);
	/// @brief すべてのパーティクルを描画する
	/// @param なし
	/// @return なし
	void Draw();

	/// @brief パーティクルグループを生成して描画資源を準備する
	/// @param name グループ名
	/// @param textureFilePath 使用テクスチャ
	/// @param verticesType 使用する頂点形状
	/// @param behavior 初期挙動
	/// @param maxInstanceCount グループごとの最大描画インスタンス数
	/// @return なし
	void CreateParticleGroup(
		const std::string& name,
		const std::string& textureFilePath,
		VerticesType verticesType = VerticesType::Quad,
		std::unique_ptr<IParticleBehavior> behavior = nullptr,
		uint32_t maxInstanceCount = kDefaultMaxParticleInstanceCount);

	/// @brief 指定グループへパーティクルを発生させる
	/// @param name 発生先グループ名
	/// @param position 発生位置
	/// @param count 発生数
	/// @return なし
	void Emit(const std::string& name, const Vector3& position, uint32_t count);

	/// @brief モデル参照を設定する
	/// @param filepath 読み込むモデルパス
	/// @return なし
	void SetModel(const std::string& filepath);

	/// @brief リング形状の頂点列を作成する
	/// @param RingDivide 円周分割数
	/// @param outerRadius 外半径
	/// @param innerRadius 内半径
	/// @return リング頂点列
	std::vector<VertexData> MakeRingVertices(uint32_t RingDivide = 128, float outerRadius = 1.0f, float innerRadius = 0.45f);

	/// @brief シリンダー形状の頂点列を作成する
	/// @param cylinderDivide 円周分割数
	/// @param topRadius 上面半径
	/// @param bottomRadius 底面半径
	/// @param height 高さ
	/// @return シリンダー頂点列
	std::vector<VertexData> MakeCylinderVertices(uint32_t cylinderDivide = 32, float topRadius = 1.0f, float bottomRadius = 1.0f, float height = 2.0f);

	/// @brief クワッド形状の頂点列を作成する
	/// @param なし
	/// @return クワッド頂点列
	std::vector<VertexData> MakeQuadVertices();

	/// @brief グループに振る舞いを設定する
	/// @param groupName 対象グループ名
	/// @param behavior 設定する振る舞い
	/// @return なし
	void SetBehavior(const std::string& groupName, std::unique_ptr<IParticleBehavior> behavior);

	/// @brief 全グループの生存パーティクル数を取得する
	/// @return 生存パーティクル数
	size_t GetTotalActiveParticleCount() const;

	/// @brief 指定グループの生存パーティクル数を取得する
	/// @param groupName 対象グループ名
	/// @return グループが存在する場合は生存数、存在しない場合は std::nullopt
	std::optional<size_t> GetActiveParticleCount(const std::string& groupName) const;

	/// @brief 指定グループの最大描画インスタンス数を取得する
	/// @param groupName 対象グループ名
	/// @return グループが存在する場合は最大数、存在しない場合は std::nullopt
	std::optional<uint32_t> GetParticleGroupMaxInstanceCount(const std::string& groupName) const;

	/// @brief 指定グループのデバッグ表示名を設定する
	/// @param groupName 対象グループ名
	/// @param debugName 表示名
	void SetParticleGroupDebugName(const std::string& groupName, const std::string& debugName);

	/// @brief 指定グループのデバッグ表示名を取得する
	/// @param groupName 対象グループ名
	/// @return グループが存在する場合は表示名、存在しない場合は std::nullopt
	std::optional<std::string> GetParticleGroupDebugName(const std::string& groupName) const;

	/// @brief 登録済みパーティクルグループ数を取得する
	/// @return グループ数
	size_t GetParticleGroupCount() const { return particleGroups.size(); }

	/// @brief 固定 deltaTime 更新を使うかを切り替える
	/// @param useFixedDeltaTime true なら 1/60 固定、false なら Update(deltaTime) の値を使う
	void SetUseFixedDeltaTime(bool useFixedDeltaTime) { useFixedDeltaTime_ = useFixedDeltaTime; }

	/// @brief 固定 deltaTime 更新中かを取得する
	/// @return 固定 deltaTime 更新中なら true
	bool IsUsingFixedDeltaTime() const { return useFixedDeltaTime_; }

	/// @brief 最後に Particle update へ適用された deltaTime を取得する
	/// @return 適用 deltaTime 秒
	float GetLastAppliedDeltaTime() const { return lastAppliedDeltaTime_; }

	/// @brief 固定更新時に使う deltaTime を取得する
	/// @return 固定 deltaTime 秒
	float GetFixedDeltaTime() const { return kFixedParticleDeltaTime; }

	/// @brief 直近 Draw で発行した particle draw call 数を取得する
	/// @return draw call 数
	uint32_t GetLastDrawCallCount() const { return lastDrawCallCount_; }

	/// @brief 直近 Draw で描画した particle instance 数を取得する
	/// @return instance 数
	uint32_t GetLastDrawnInstanceCount() const { return lastDrawnInstanceCount_; }

private:
	static constexpr float kFixedParticleDeltaTime = 1.0f / 60.0f;
	static constexpr float kMaxParticleDeltaTime = 1.0f / 15.0f;

	float ResolveDeltaTime(float deltaTime) const;
	void UpdateParticleGroup(ParticleGroup& particleGroup, float deltaTime, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
	void UpdateAliveParticle(Particle& particle, ParticleGroup& particleGroup, uint32_t& counter, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
	void InitializeParticleGroupMaterial(ParticleGroup& particleGroup);
	void InitializeParticleGroupVertices(ParticleGroup& particleGroup, VerticesType verticesType);
	void InitializeParticleGroupTexture(ParticleGroup& particleGroup, const std::string& textureFilePath);
	void InitializeParticleGroupInstances(ParticleGroup& particleGroup);

	Engine::Base::DirectXCommon* dxCommon_=nullptr;
	Engine::Base::SrvManager* srvManager_ = nullptr;


	std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_;

	Engine::Graphics3D::Model* model_ = nullptr;
	bool useFixedDeltaTime_ = true;
	float lastAppliedDeltaTime_ = kFixedParticleDeltaTime;
	uint32_t lastDrawCallCount_ = 0;
	uint32_t lastDrawnInstanceCount_ = 0;

	std::mt19937 randomEngine;

	std::unordered_map<std::string, ParticleGroup> particleGroups;
};

}

