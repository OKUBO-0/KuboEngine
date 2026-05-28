#include "Object3DCommon.h"
#include "Object3D.h"
#include "DirectXCommon.h"
#include "Model.h"
#include "MyMath.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr float kFixedAnimationDeltaTime = 1.0f / 60.0f;
constexpr float kDefaultEnvironmentReflectionStrength = 0.5f;
constexpr float kDefaultEnvironmentRoughness = 0.5f;
}

namespace Engine::Graphics3D {

void Object3D::Initialize(Object3DCommon* object3DCommon)
{
	object3DCommon_ = object3DCommon;
	InitializeTransformResources();
	InitializeLightResources();
	InitializeEnvironmentResources();
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f} ,{0.0f,0.0f,0.0f} };
	InitializeCameraResources();
}

void Object3D::Update()
{
	UpdateAnimationState();
	ApplyModelSettings();
	UpdateTransformationMatrices();
}

void Object3D::SkeletonUpdate(Skeleton& skeleton)
{
	// ← ここでサイズを合わせるのが絶対必要！！
	skeletonPose_.resize(skeleton.joints.size());
	//すべてのjointを更新。親が若いので通常ループで処理可能になっている
	for (Joint& joint : skeleton.joints)
	{
		joint.localMatrix = MyMath::MakeAffineMatrix(joint.transform.scale, joint.transform.rotate, joint.transform.translate);
		if (joint.parent)
		{
			joint.skeletonSpaceMatrix = joint.localMatrix * skeleton.joints[*joint.parent].skeletonSpaceMatrix;

		} else
		{
			joint.skeletonSpaceMatrix = joint.localMatrix;
		}
		skeletonPose_[joint.index] = joint.skeletonSpaceMatrix;
	}
	
}

void Object3D::ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime)
{
	for (Joint& joint : skeleton.joints) {
		// 対象のJointのAnimationがあれば、値の適用を行う。
		// 下記のif文はC++17から可能になった初期化付きif文。
		if (auto it = animation.nodeAnimations.find(joint.name); it != animation.nodeAnimations.end()) {
			const NodeAnimation& nodeAnimation = it->second;

			joint.transform.translate = CalculateValue(nodeAnimation.translate, animationTime);
			joint.transform.rotate = CalculateValue(nodeAnimation.rotate, animationTime);
			joint.transform.scale = CalculateValue(nodeAnimation.scale, animationTime);
		}
	}
}

void Object3D::SkinClusterUpdate(SkinCluster& skinCluster, const Skeleton& skeleton)
{
	for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
	{
		assert(jointIndex < skinCluster.inverseBindPoseMatrices.size());
		skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix =
			skinCluster.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
		skinCluster.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
			MyMath::Transpose(skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix.Inverse());
	}


}




void Object3D::Draw()
{


	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());
	//平行光源Cbufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
	//カメラのデータをセット
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
	//ポイントライトのCBufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
	//スポットライトのCBufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());
	//環境マップテクスチャ
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(7, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	//環境マップの反射率ぼかし
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(8, environmentReflectionSettingResource->GetGPUVirtualAddress());
	//3Dモデルが割り当てられているなら描画する
	if (model_) {
		model_->Draw();
	}


}

void Object3D::DrawSkinning()
{


	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());
	//平行光源Cbufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
	//カメラのデータをセット
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
	//ポイントライトのCBufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
	//スポットライトのCBufferの場所を設定
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());
	//skeletonのデータをセット
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootDescriptorTable(7, model_->GetSkinCluster().paletteSrvHandle.second);
	//環境マップテクスチャ
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(8, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	//環境マップの反射率ぼかし
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(9, environmentReflectionSettingResource->GetGPUVirtualAddress());
	//3Dモデルが割り当てられているなら描画する
	if (model_) {
		model_->Draw();
	}

}


void Object3D::SetModel(const std::string& filepath)
{
	// モデルを検索してセットする
	model_ = ModelManager::GetInstance()->FindModel(filepath);
}

void Object3D::SetModelFromResourceRoot(const std::string& resourceRoot, const std::string& filepath)
{
	model_ = ModelManager::GetInstance()->FindModelFromResourceRoot(resourceRoot, filepath);
}

float Object3D::GetScaledModelBoundingRadius(float fallback) const
{
	if (!model_ || !model_->HasBounds()) {
		return fallback;
	}

	const float maxScale = (std::max)({
		std::abs(transform.scale.x),
		std::abs(transform.scale.y),
		std::abs(transform.scale.z),
	});
	return model_->GetLocalBoundingRadius() * (std::max)(0.01f, maxScale);
}

Engine::Math::AABB Object3D::GetScaledModelAabb(float fallbackRadius) const
{
	if (!model_ || !model_->HasBounds()) {
		const Vector3 extent{ fallbackRadius, fallbackRadius, fallbackRadius };
		return {
			{
				transform.translate.x - extent.x,
				transform.translate.y - extent.y,
				transform.translate.z - extent.z,
			},
			{
				transform.translate.x + extent.x,
				transform.translate.y + extent.y,
				transform.translate.z + extent.z,
			},
		};
	}

	const ModelData& modelData = model_->GetModelData();
	const Vector3 scaledMin{
		modelData.localAabbMin.x * transform.scale.x,
		modelData.localAabbMin.y * transform.scale.y,
		modelData.localAabbMin.z * transform.scale.z,
	};
	const Vector3 scaledMax{
		modelData.localAabbMax.x * transform.scale.x,
		modelData.localAabbMax.y * transform.scale.y,
		modelData.localAabbMax.z * transform.scale.z,
	};

	return {
		{
			transform.translate.x + (std::min)(scaledMin.x, scaledMax.x),
			transform.translate.y + (std::min)(scaledMin.y, scaledMax.y),
			transform.translate.z + (std::min)(scaledMin.z, scaledMax.z),
		},
		{
			transform.translate.x + (std::max)(scaledMin.x, scaledMax.x),
			transform.translate.y + (std::max)(scaledMin.y, scaledMax.y),
			transform.translate.z + (std::max)(scaledMin.z, scaledMax.z),
		},
	};
}

Engine::Math::OBB Object3D::GetScaledModelObb(float fallbackRadius) const
{
	const Matrix4x4 rotation = MyMath::MakeRotateMatrix(transform.rotate);
	Engine::Math::OBB obb{};
	obb.orientations[0] = MyMath::Normalize(Vector3{ rotation.m[0][0], rotation.m[0][1], rotation.m[0][2] });
	obb.orientations[1] = MyMath::Normalize(Vector3{ rotation.m[1][0], rotation.m[1][1], rotation.m[1][2] });
	obb.orientations[2] = MyMath::Normalize(Vector3{ rotation.m[2][0], rotation.m[2][1], rotation.m[2][2] });

	if (!model_ || !model_->HasBounds()) {
		obb.center = transform.translate;
		obb.size = { fallbackRadius, fallbackRadius, fallbackRadius };
		return obb;
	}

	const ModelData& modelData = model_->GetModelData();
	const Vector3 localCenter = modelData.localAabbCenter;
	const Vector3 localSize{
		(modelData.localAabbMax.x - modelData.localAabbMin.x) * 0.5f,
		(modelData.localAabbMax.y - modelData.localAabbMin.y) * 0.5f,
		(modelData.localAabbMax.z - modelData.localAabbMin.z) * 0.5f,
	};
	obb.center = MyMath::Transform(
		{
			localCenter.x * transform.scale.x,
			localCenter.y * transform.scale.y,
			localCenter.z * transform.scale.z,
		},
		rotation);
	obb.center += transform.translate;
	obb.size = {
		std::abs(localSize.x * transform.scale.x),
		std::abs(localSize.y * transform.scale.y),
		std::abs(localSize.z * transform.scale.z),
	};
	return obb;
}

void Object3D::InitializeTransformResources()
{
	transformationMatrixResource = object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
	transformationMatrixData_->WVP = transformationMatrixData_->WVP.MakeIdentity4x4();
	transformationMatrixData_->World = transformationMatrixData_->World.MakeIdentity4x4();
	transformationMatrixData_->worldInverseTranspose =
		transformationMatrixData_->worldInverseTranspose.MakeIdentity4x4();
}

void Object3D::InitializeLightResources()
{
	directionalLightResource = object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,1.0f };
	directionalLightData->intensity = 1.0f;
	directionalLightData->enable = 1;

	pointLightResource = object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(PointLight));
	pointLightResource->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData));
	pointLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	pointLightData->position = { 0.0f,0.0f,0.0f };
	pointLightData->intensity = 1.0f;
	pointLightData->radius = 10.0f;
	pointLightData->decay = 1.0f;
	pointLightData->enable = 0;

	spotLightResource = object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(SpotLight));
	spotLightResource->Map(0, nullptr, reinterpret_cast<void**>(&spotLightData));
	spotLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	spotLightData->position = { 0.0f,2.0f,0.0f };
	spotLightData->intensity = 4.0f;
	spotLightData->direction = MyMath::Normalize(Vector3{ 0.0f,-1.0f,0.0f });
	spotLightData->distance = 7.0f;
	spotLightData->decay = 2.0f;
	spotLightData->coneAngleCos = std::cos(std::numbers::pi_v<float> / 3.0f);
	spotLightData->cosFalloffStart = 1.0f;
	spotLightData->enable = 0;
}

void Object3D::InitializeEnvironmentResources()
{
	environmentReflectionSettingResource =
		object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(EnvironmentReflectionSetting));
	environmentReflectionSettingResource->Map(
		0, nullptr, reinterpret_cast<void**>(&environmentReflectionSettingData));
	environmentReflectionSettingData->reflectionStrength = kDefaultEnvironmentReflectionStrength;
	environmentReflectionSettingData->roughness = kDefaultEnvironmentRoughness;
}

void Object3D::InitializeCameraResources()
{
	cameraResource = object3DCommon_->GetDxCommon()->CreateBufferResource(sizeof(CameraForGpu));
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraForGpu));
}

void Object3D::UpdateAnimationState()
{
	if (!enableAnimation_ || !model_ || model_->GetAnimation().nodeAnimations.empty()) {
		return;
	}

	ApplyAnimation(model_->GetSkeleton(), model_->GetAnimation(), animationTime);
	SkeletonUpdate(model_->GetSkeleton());
	SkinClusterUpdate(model_->GetSkinCluster(), model_->GetSkeleton());
	animationTime += kFixedAnimationDeltaTime;
	animationTime = std::fmod(animationTime, model_->GetAnimation().duration);
}

void Object3D::ApplyModelSettings()
{
	if (!model_) {
		return;
	}

	model_->SetEnableLighting(enableLighting);
	model_->SetColor(color_);
}

void Object3D::UpdateTransformationMatrices()
{
	worldMatrix = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	transformationMatrixData_->World = worldMatrix;
	transformationMatrixData_->worldInverseTranspose = MyMath::Transpose(worldMatrix.Inverse());

	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		worldViewProjectionMatrix = worldMatrix;
		transformationMatrixData_->WVP = worldViewProjectionMatrix;
		return;
	}

	worldViewProjectionMatrix = worldMatrix * activeCamera->GetViewProjectionMatrix();
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
	cameraForGpu->worldPosition = activeCamera->GetTransform().translate;
}



Vector3 Object3D::CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time)
{
	assert(!keyframes.empty());
	if (keyframes.size() == 1 || time <= keyframes[0].time) {
		return keyframes[0].value;
	}

	for (size_t index = 0; index < keyframes.size() - 1; ++index) {
		size_t nextIndex = index + 1;
		//indexとnextIndexの2つのキーフレームを取得して範囲内に時刻があるか判定する
		if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
			//補間する
			float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
			return MyMath::Lerp(keyframes[index].value, keyframes[nextIndex].value, t);
		}

	}
	return (*keyframes.rbegin()).value;
}

Quaternion Object3D::CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time)
{

	assert(!keyframes.empty());
	if (keyframes.size() == 1 || time <= keyframes[0].time) {
		return keyframes[0].value;
	}
	for (size_t index = 0; index < keyframes.size() - 1; ++index) {
		size_t nextIndex = index + 1;
		//indexとnextIndexの2つのキーフレームを取得して範囲内に時刻があるか判定する
		if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
			//補間する
			float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
			return MyMath::Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
		}
	}
	return (*keyframes.rbegin()).value;
}

}

