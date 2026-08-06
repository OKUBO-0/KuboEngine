#pragma once
#include "RenderingData.h"
#include <d3d12.h>
#include <array>
#include <memory>
#include <wrl.h>

namespace Engine::Base {
class DirectXCommon;
class GraphicsPipeline;
class SrvManager;
}

/// @brief 3D オブジェクト描画の共通設定を管理するクラス
/// @details 通常描画とスキニング描画のパイプラインを初期化し、
///          各 Object3D が使う共通レンダリング状態を適用する。
namespace Engine::Graphics3D {

class Object3DCommon
{
public:
	struct ShadowPassStats {
		uint32_t candidateCount = 0;
		uint32_t submittedCount = 0;
		uint32_t culledCount = 0;
	};
	struct DrawCullStats {
		uint32_t candidateCount = 0;
		uint32_t submittedCount = 0;
		uint32_t culledCount = 0;
	};
	struct SkinningCacheStats {
		uint32_t hitCount = 0;
		uint32_t missCount = 0;
		uint32_t gpuUploadHitCount = 0;
		uint32_t gpuUploadMissCount = 0;
	};
	struct InstanceBatchStats {
		uint32_t objectBatchCount = 0;
		uint32_t objectInstanceCount = 0;
		uint32_t skinningBatchCount = 0;
		uint32_t skinningInstanceCount = 0;
		uint32_t queuedObjectCount = 0;
		uint32_t queuedStaticObjectCount = 0;
		uint32_t queuedSkinningObjectCount = 0;
		uint32_t queuedShadowObjectCount = 0;
	};


	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return Object3DCommon のインスタンス
	static Object3DCommon* GetInstance();



	/// @brief 3D 描画共通リソースを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager);

	/// @brief 共通管理インスタンスを解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief 通常 3D 描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void CommonDraw();
	void ObjectInstancingCommonDraw();

	/// @brief スキニング 3D 描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void SkinningCommonDraw();
	void SkinningInstancingCommonDraw();
	void ShadowCommonDraw();
	void ObjectInstancingShadowCommonDraw();
	void SkinningShadowCommonDraw();
	void SkinningInstancingShadowCommonDraw();
	uint32_t BindSkinningInstanceTransforms(
		ID3D12Resource* resource,
		UINT numElements,
		UINT64 byteOffset,
		UINT structureByteStride);
	uint32_t BindObjectInstanceData(
		ID3D12Resource* resource,
		UINT numElements,
		UINT64 byteOffset,
		UINT structureByteStride);
	bool BeginShadowPass(const Vector3& focusPosition);
	void EndShadowPass();
	void BindSceneLighting(bool skinning = false);
	bool IsShadowPassActive() const { return shadowPassActive_; }
	bool IsInsideShadowFrustum(const Vector3& worldCenter, float boundingRadius) const;
	void RecordShadowCandidate(bool submitted);
	void RecordDrawCandidate(bool submitted);
	void RecordSkinningCacheHit();
	void RecordSkinningCacheMiss();
	void RecordSkinningGpuUploadCacheHit();
	void RecordSkinningGpuUploadCacheMiss();
	void RecordObjectInstanceBatch(uint32_t instanceCount);
	void RecordSkinningInstanceBatch(uint32_t instanceCount);
	void RecordRenderQueueFlush(
		uint32_t queuedCount,
		uint32_t staticCount,
		uint32_t skinningCount);
	void RecordShadowQueueFlush(uint32_t queuedCount);
	const ShadowPassStats& GetShadowPassStats() const { return shadowPassStats_; }
	const DrawCullStats& GetDrawCullStats() const { return drawCullStats_; }
	const SkinningCacheStats& GetSkinningCacheStats() const { return skinningCacheStats_; }
	const InstanceBatchStats& GetInstanceBatchStats() const { return instanceBatchStats_; }
	void ResetShadowPassStatistics();
	void ResetDrawCullStatistics();
	void ResetSkinningCacheStatistics();
	void ResetInstanceBatchStatistics();
	uint64_t GetMeasuredShadowPassCount() const { return measuredShadowPassCount_; }
	uint64_t GetTotalShadowCandidateCount() const { return totalShadowCandidateCount_; }
	uint64_t GetTotalShadowSubmittedCount() const { return totalShadowSubmittedCount_; }
	uint64_t GetTotalShadowCulledCount() const { return totalShadowCulledCount_; }
	uint32_t GetShadowMapSize() const { return kShadowMapSize; }
	uint64_t GetShadowMapMemoryBytes() const
	{
		return static_cast<uint64_t>(kShadowMapSize) * kShadowMapSize * sizeof(float);
	}
	void SetSceneLight(const SceneLightData& light);
	const SceneLightData& GetSceneLight() const { return sceneLightData_; }
	void SetShadowEnabled(bool enabled);
	bool IsShadowEnabled() const;
	void SetShadowStrength(float strength);
	float GetShadowStrength() const;
	void SetShadowSoftness(float texels);
	float GetShadowSoftness() const;
	void SetShadowBias(float bias);
	float GetShadowBias() const;
	void SetShadowArea(float area);
	float GetShadowArea() const { return shadowArea_; }

	/// @brief DirectX 共通管理を shared_ptr で返し、外部利用中の use-after-free を防ぐ
	std::shared_ptr<Engine::Base::DirectXCommon> GetDxCommon() const { return dxCommon_.lock(); }
	//SrvManager
	Engine::Base::SrvManager* GetSrvManager()const { return srvManager_; }

	

private:

	Object3DCommon() = default;
	~Object3DCommon();
	Object3DCommon(const Object3DCommon&) = delete;
	Object3DCommon& operator=(const Object3DCommon&) = delete;

private:
	std::shared_ptr<Engine::Base::DirectXCommon> GetDirectXCommon() const;
	uint32_t BindInstanceStructuredBuffer(
		UINT rootParameterIndex,
		ID3D12Resource* resource,
		UINT numElements,
		UINT64 byteOffset,
		UINT structureByteStride);
	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	Engine::Base::SrvManager* srvManager_ = nullptr;

	std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_;
	std::unique_ptr<Engine::Base::GraphicsPipeline> skinningGraphicsPipeline_;
	std::unique_ptr<Engine::Base::GraphicsPipeline> shadowGraphicsPipeline_;
	SceneLightData sceneLightData_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_;
	ShadowMapData shadowMapData_{};
	D3D12_GPU_VIRTUAL_ADDRESS shadowMapDataGpuAddress_ = 0;
	uint32_t shadowSrvIndex_ = UINT32_MAX;
	bool shadowPassActive_ = false;
	bool shadowEnabled_ = true;
	float shadowArea_ = 72.0f;
	ShadowPassStats shadowPassStats_{};
	DrawCullStats drawCullStats_{};
	SkinningCacheStats skinningCacheStats_{};
	InstanceBatchStats instanceBatchStats_{};
	static constexpr uint32_t kBufferedFrameCount = 2;
	static constexpr uint32_t kSkinningInstanceSrvCountPerFrame = 192;
	std::array<std::array<uint32_t, kSkinningInstanceSrvCountPerFrame>, kBufferedFrameCount>
		skinningInstanceSrvIndices_{};
	std::array<uint32_t, kBufferedFrameCount> skinningInstanceSrvCursor_{};
	std::array<uint64_t, kBufferedFrameCount> skinningInstanceSrvFrameSerials_{};
	bool skinningInstanceSrvInitialized_ = false;
	uint64_t measuredShadowPassCount_ = 0;
	uint64_t totalShadowCandidateCount_ = 0;
	uint64_t totalShadowSubmittedCount_ = 0;
	uint64_t totalShadowCulledCount_ = 0;
	static constexpr uint32_t kShadowMapSize = 2048;
};

}

