#pragma once
#include "MyMath.h"
#include "Model.h"
#include "RenderingData.h"
#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Graphics3D {

class Object3DCommon;

struct EnvironmentReflectionSetting {
	float reflectionStrength = 1.0f; // 反射の強さ（0 = 無効、1 = 最大）
	float roughness = 0.0f;          // 反射のぼかし（0 = 鏡面、1 = ぼやけ）
	float textureInfluence = 1.0f;
	float padding = 0.0f;
};

/// @brief 3Dモデルの描画状態を保持し、更新と描画を担当するクラス
/// @details トランスフォーム、ライト、カメラ、スキニング、環境反射設定をまとめて扱う。
class Object3D
{
public:
	~Object3D();

	/// @brief 描画に必要なGPUリソースを初期化する
	/// @param object3DCommon 共通描画設定
	/// @return なし
	void Initialize(Object3DCommon* object3DCommon);

	/// @brief アニメーションと行列定数を更新する
	/// @param なし
	/// @return なし
	void Update();

	void SkeletonUpdate( Skeleton& skeleton);
	void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime);
	void SkinClusterUpdate(const SkinCluster& skinCluster, const Skeleton& skeleton);
	/// @brief 通常描画用のコマンドを積む
	/// @param なし
	/// @return なし
	void Draw();
	/// @brief スキニング描画用のコマンドを積む
	/// @param なし
	/// @return なし
	void DrawSkinning();
	void DrawShadow();
	void SetCastsShadow(bool castsShadow) { castsShadow_ = castsShadow; }
	bool CastsShadow() const { return castsShadow_; }



	void SetModel(Model* model);
	void SetModel(const std::string& filepath);
	void SetModelFromResourceRoot(const std::string& resourceRoot, const std::string& filepath);
	float GetScaledModelBoundingRadius(float fallback = 1.0f) const;
	Engine::Math::AABB GetScaledModelAabb(float fallbackRadius = 1.0f) const;
	Engine::Math::OBB GetScaledModelObb(float fallbackRadius = 1.0f) const;

	//環境マップ
	void SetSkyboxFilePath(const std::string& filepath) { skyboxFilePath_ = filepath; }
	void SetEnvironmentReflectionStrength(float reflectionStrength) { environmentReflectionSettingData_.reflectionStrength = reflectionStrength; }
	void SetEnvironmentRoughness(float roughness) { environmentReflectionSettingData_.roughness = roughness; }
	void SetTextureInfluence(float influence) { environmentReflectionSettingData_.textureInfluence = influence; }
	float GetEnvironmentReflectionStrength() const { return environmentReflectionSettingData_.reflectionStrength; }
	float GetEnvironmentRoughness() const { return environmentReflectionSettingData_.roughness; }

	// transform
	void SetTransform(const EulerTransform& transform) { this->transform = transform; }
	const EulerTransform& GetTransform() const { return transform; }

	

	//スケール
	void SetScale(const Vector3& scale) { transform.scale = scale; }
	//回転
	void SetRotate(const Vector3& rotate) { transform.rotate = rotate; }
	//位置
	void SetTranslate(const Vector3& translate) { transform.translate = translate; }

	//ディレクションライト
	void SetDirectionalLight(const DirectionalLight& directionalLight) { directionalLightData_ = directionalLight; }
	const DirectionalLight& GetDirectionalLight() const { return directionalLightData_; }
	//ディレクションライトの向き
	void SetDirectionalLightDirection(const Vector3& direction) { directionalLightData_.direction = direction; }
	//ディレクションライトの色
	void SetDirectionalLightColor(const Vector4& color) { directionalLightData_.color = color; }
	//ディレクションライトの強さ
	void SetDirectionalLightIntensity(float intensity) { directionalLightData_.intensity = intensity; }
	//ライトオンオフ
	void SetDirectionalLightEnable(bool enable) { directionalLightData_.enable = enable; }

	//ポイントライト
	void SetPointLight(const PointLight& pointLight) { pointLightData_ = pointLight; }
	const PointLight& GetPointLight() const { return pointLightData_; }
	//ポイントライトの位置
	void SetPointLightPosition(const Vector3& position) { pointLightData_.position = position; }
	//ポイントライトの色
	void SetPointLightColor(const Vector4& color) { pointLightData_.color = color; }
	//ポイントライトの強さ
	void SetPointLightIntensity(float intensity) { pointLightData_.intensity = intensity; }
	//ポイントライトの半径
	void SetPointLightRadius(float radius) { pointLightData_.radius = radius; }
	float GetPointLightRadius() { return pointLightData_.radius; }
	//ポイントライトの減衰率
	void SetPointLightDecay(float decay) { pointLightData_.decay = decay; }
	float GetPointLightDecay() { return pointLightData_.decay; }
	//ポイントライトのオンオフ
	void SetPointLightEnable(bool enable) { pointLightData_.enable = enable; }


	//スポットライト
	void SetSpotLight(const SpotLight& spotLight) { spotLightData_ = spotLight; }
	const SpotLight& GetSpotLight() const { return spotLightData_; }
	//スポットライトの位置
	void SetSpotLightPosition(const Vector3& position) { spotLightData_.position = position; }
	//スポットライトの向き
	void SetSpotLightDirection(const Vector3& direction) { spotLightData_.direction = direction; }
	//スポットライトの色
	void SetSpotLightColor(const Vector4& color) { spotLightData_.color = color; }
	//スポットライトの強さ
	void SetSpotLightIntensity(float intensity) { spotLightData_.intensity = intensity; }
	//スポットライトの距離
	void SetSpotLightDistance(float distance) { spotLightData_.distance = distance; }
	//スポットライトの減衰率
	void SetSpotLightDecay(float decay) { spotLightData_.decay = decay; }
	//スポットライトのコーンの角度
	void SetSpotLightConeAngleCos(float coneAngleCos) { spotLightData_.coneAngleCos = coneAngleCos; }

	void SetSpotLightCosFalloffStart(float cosFalloffStart) { spotLightData_.cosFalloffStart = cosFalloffStart; }
	//スポットライトのオンオフ
	void SetSpotLightEnable(bool enable) { spotLightData_.enable = enable; }

	//ライトのオンオフ
	void SetLighting(bool enable) { enableLighting = enable; }

	void SetColor(const Vector4& color) { color_ = color; }
	const Vector4& GetColor() const { return color_; }

	void SetDebugName(const std::string& debugName) { debugName_ = debugName; }
	const std::string& GetDebugName() const { return debugName_; }

	//アニメーション
	Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
	Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);
	
	


private:
	void InitializeTransformResources();
	void InitializeMaterialResources();
	void InitializeLightResources();
	void InitializeEnvironmentResources();
	void InitializeCameraResources();
	void InitializeSkinningState();
	void ReleaseSkinningDescriptors();
	void UpdateAnimationState();
	void ApplyModelSettings();
	void UpdateTransformationMatrices();
	D3D12_GPU_VIRTUAL_ADDRESS UploadFrameConstant(const void* data, size_t size);

	Object3DCommon* object3DCommon_ = nullptr;//Object3DCommonのポインタ

	Model* model_ = nullptr;//モデルのポインタ

	//トランスフォーム
	TransformationMatrix transformationMatrixData_{};
	Material materialData_{};


	// Compatibility/debug state. Rendering uses Object3DCommon scene lighting.
	DirectionalLight directionalLightData_{};
	PointLight pointLightData_{};
	SpotLight spotLightData_{};

	//SRT
	EulerTransform transform;
	Matrix4x4 worldMatrix;
	Matrix4x4 worldViewProjectionMatrix;

	//ライトのオンオフ
	bool enableLighting = true;
	bool castsShadow_ = true;
	//カメラforGPU
	CameraForGpu cameraForGpu_{};
	//アニメーション
	float animationTime = 0.0f;
	bool enableAnimation_= true;

	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // デフォルトは白
	static constexpr uint32_t kBufferedFrameCount = 2;
	Skeleton skeleton_{};
	std::vector<Matrix4x4> skeletonPose_;
	std::vector<WellForGPU> skinPaletteData_;
	std::array<uint32_t, kBufferedFrameCount> skinPaletteSrvIndices_{
		UINT32_MAX,
		UINT32_MAX,
	};

	std::string debugName_;
	std::string skyboxFilePath_ ; // スカイボックスのファイルパス
	EnvironmentReflectionSetting environmentReflectionSettingData_; // 環境反射設定

};

}

