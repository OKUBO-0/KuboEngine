#pragma once
#include "MyMath.h"
#include "RenderingData.h"
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>

namespace Engine::Graphics3D {

class Model;
class Object3DCommon;
struct SkinCluster;

struct EnvironmentReflectionSetting {
	float reflectionStrength = 1.0f; // 反射の強さ（0 = 無効、1 = 最大）
	float roughness = 0.0f;          // 反射のぼかし（0 = 鏡面、1 = ぼやけ）
	float padding[2] = {};           // HLSLと同様に16バイト境界を守るためのパディング
};

/// @brief 3Dモデルの描画状態を保持し、更新と描画を担当するクラス
/// @details トランスフォーム、ライト、カメラ、スキニング、環境反射設定をまとめて扱う。
class Object3D
{
public:
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
	void SkinClusterUpdate(SkinCluster&skinCluster,const Skeleton&skeleton);
	/// @brief 通常描画用のコマンドを積む
	/// @param なし
	/// @return なし
	void Draw();
	/// @brief スキニング描画用のコマンドを積む
	/// @param なし
	/// @return なし
	void DrawSkinning();



	void SetModel(Model* model) { model_ = model; }
	void SetModel(const std::string& filepath);
	void SetModelFromResourceRoot(const std::string& resourceRoot, const std::string& filepath);
	float GetScaledModelBoundingRadius(float fallback = 1.0f) const;
	Engine::Math::AABB GetScaledModelAabb(float fallbackRadius = 1.0f) const;
	Engine::Math::OBB GetScaledModelObb(float fallbackRadius = 1.0f) const;

	//環境マップ
	void SetSkyboxFilePath(const std::string& filepath) { skyboxFilePath_ = filepath; }
	void SetEnvironmentReflectionStrength(float reflectionStrength) { environmentReflectionSettingData->reflectionStrength = reflectionStrength; }
	void SetEnvironmentRoughness(float roughness) { environmentReflectionSettingData->roughness = roughness; }
	float GetEnvironmentReflectionStrength() { return environmentReflectionSettingData->reflectionStrength; }
	float GetEnvironmentRoughness() { return environmentReflectionSettingData->roughness; }

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
	void SetDirectionalLight(const DirectionalLight& directionalLight) { *directionalLightData = directionalLight; }
	const DirectionalLight& GetDirectionalLight() const { return *directionalLightData; }
	//ディレクションライトの向き
	void SetDirectionalLightDirection(const Vector3& direction) { directionalLightData->direction = direction; }
	//ディレクションライトの色
	void SetDirectionalLightColor(const Vector4& color) { directionalLightData->color = color; }
	//ディレクションライトの強さ
	void SetDirectionalLightIntensity(float intensity) { directionalLightData->intensity = intensity; }
	//ライトオンオフ
	void SetDirectionalLightEnable(bool enable) { directionalLightData->enable = enable; }

	//ポイントライト
	void SetPointLight(const PointLight& pointLight) { *pointLightData = pointLight; }
	const PointLight& GetPointLight() const { return *pointLightData; }
	//ポイントライトの位置
	void SetPointLightPosition(const Vector3& position) { pointLightData->position = position; }
	//ポイントライトの色
	void SetPointLightColor(const Vector4& color) { pointLightData->color = color; }
	//ポイントライトの強さ
	void SetPointLightIntensity(float intensity) { pointLightData->intensity = intensity; }
	//ポイントライトの半径
	void SetPointLightRadius(float radius) { pointLightData->radius = radius; }
	float GetPointLightRadius() { return pointLightData->radius; }
	//ポイントライトの減衰率
	void SetPointLightDecay(float decay) { pointLightData->decay = decay; }
	float GetPointLightDecay() { return pointLightData->decay; }
	//ポイントライトのオンオフ
	void SetPointLightEnable(bool enable) { pointLightData->enable = enable; }


	//スポットライト
	void SetSpotLight(const SpotLight& spotLight) { *spotLightData = spotLight; }
	const SpotLight& GetSpotLight() const { return *spotLightData; }
	//スポットライトの位置
	void SetSpotLightPosition(const Vector3& position) { spotLightData->position = position; }
	//スポットライトの向き
	void SetSpotLightDirection(const Vector3& direction) { spotLightData->direction = direction; }
	//スポットライトの色
	void SetSpotLightColor(const Vector4& color) { spotLightData->color = color; }
	//スポットライトの強さ
	void SetSpotLightIntensity(float intensity) { spotLightData->intensity = intensity; }
	//スポットライトの距離
	void SetSpotLightDistance(float distance) { spotLightData->distance = distance; }
	//スポットライトの減衰率
	void SetSpotLightDecay(float decay) { spotLightData->decay = decay; }
	//スポットライトのコーンの角度
	void SetSpotLightConeAngleCos(float coneAngleCos) { spotLightData->coneAngleCos = coneAngleCos; }

	void SetSpotLightCosFalloffStart(float cosFalloffStart) { spotLightData->cosFalloffStart = cosFalloffStart; }
	//スポットライトのオンオフ
	void SetSpotLightEnable(bool enable) { spotLightData->enable = enable; }

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
	void InitializeLightResources();
	void InitializeEnvironmentResources();
	void InitializeCameraResources();
	void UpdateAnimationState();
	void ApplyModelSettings();
	void UpdateTransformationMatrices();

	Object3DCommon* object3DCommon_ = nullptr;//Object3DCommonのポインタ

	Model* model_ = nullptr;//モデルのポインタ

	//トランスフォーム
	//ModelTransform用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource;
	//データを書き込む

	TransformationMatrix* transformationMatrixData_ = nullptr;


	//平行光源
	//平行光源用のResourceを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource;
	DirectionalLight* directionalLightData = nullptr;

	//ポイントライト
	//ポイントライト用のリソースを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource;
	PointLight* pointLightData = nullptr;

	//スポットライト
	//スポットライト用のリソースを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource;
	SpotLight* spotLightData = nullptr;

	//SRT
	EulerTransform transform;
	Matrix4x4 worldMatrix;
	Matrix4x4 worldViewProjectionMatrix;

	//ライトのオンオフ
	bool enableLighting = true;
	//カメラforGPU
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource;//カメラのデータを送るためのリソース
	CameraForGpu* cameraForGpu = nullptr;//カメラのデータをGPUに送るための構造体
	//アニメーション
	float animationTime = 0.0f;
	bool enableAnimation_= true;

	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // デフォルトは白
	std::vector<Matrix4x4> skeletonPose_;

	std::string debugName_;
	std::string skyboxFilePath_ ; // スカイボックスのファイルパス
	EnvironmentReflectionSetting* environmentReflectionSettingData; // 環境反射設定
	Microsoft::WRL::ComPtr<ID3D12Resource> environmentReflectionSettingResource;

};

}

