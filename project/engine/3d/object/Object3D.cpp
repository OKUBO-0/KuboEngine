#include "Object3DCommon.h"
#include "Object3D.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "Model.h"
#include "MyMath.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace {
constexpr float kFixedAnimationDeltaTime = 1.0f / 60.0f;
constexpr float kDefaultEnvironmentReflectionStrength = 0.5f;
constexpr float kDefaultEnvironmentRoughness = 0.5f;
}

namespace Engine::Graphics3D {

Object3D::~Object3D()
{
	ReleaseSkinningDescriptors();
}

void Object3D::Initialize(Object3DCommon* object3DCommon)
{
	object3DCommon_ = object3DCommon;
	InitializeTransformResources();
	InitializeMaterialResources();
	InitializeLightResources();
	InitializeEnvironmentResources();
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f} ,{0.0f,0.0f,0.0f} };
	InitializeCameraResources();
	InitializeSkinningState();
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

			if (!nodeAnimation.translate.empty()) {
				joint.transform.translate =
					CalculateValue(nodeAnimation.translate, animationTime);
			}
			if (!nodeAnimation.rotate.empty()) {
				joint.transform.rotate =
					CalculateValue(nodeAnimation.rotate, animationTime);
			}
			if (!nodeAnimation.scale.empty()) {
				joint.transform.scale =
					CalculateValue(nodeAnimation.scale, animationTime);
			}
		}
	}
}

void Object3D::SkinClusterUpdate(const SkinCluster& skinCluster, const Skeleton& skeleton)
{
	skinPaletteData_.resize(skeleton.joints.size());
	for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
	{
		assert(jointIndex < skinCluster.inverseBindPoseMatrices.size());
		skinPaletteData_[jointIndex].skeletonSpaceMatrix =
			skinCluster.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
		skinPaletteData_[jointIndex].skeletonSpaceInverseTransposeMatrix =
			MyMath::Transpose(skinPaletteData_[jointIndex].skeletonSpaceMatrix.Inverse());
	}
}




void Object3D::Draw()
{
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
		UploadFrameConstant(&cameraForGpu_, sizeof(cameraForGpu_));
	const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
		UploadFrameConstant(&environmentReflectionSettingData_, sizeof(environmentReflectionSettingData_));
	const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
		UploadFrameConstant(&materialData_, sizeof(materialData_));

	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformAddress);
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(4, cameraAddress);
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(5, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(6, environmentAddress);
	object3DCommon_->BindSceneLighting();
	if (model_) {
		model_->Draw(materialAddress);
	}
}

void Object3D::DrawSkinning()
{
	if (!model_ || skinPaletteData_.empty()) {
		return;
	}

	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
		UploadFrameConstant(&cameraForGpu_, sizeof(cameraForGpu_));
	const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
		UploadFrameConstant(&environmentReflectionSettingData_, sizeof(environmentReflectionSettingData_));
	const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
		UploadFrameConstant(&materialData_, sizeof(materialData_));
	const size_t paletteBytes = sizeof(WellForGPU) * skinPaletteData_.size();
	const Engine::Base::DirectXCommon::FrameUploadAllocation paletteAllocation =
		object3DCommon_->GetDxCommon()->AllocateFrameUpload(paletteBytes, sizeof(WellForGPU));
	std::memcpy(paletteAllocation.cpuAddress, skinPaletteData_.data(), paletteBytes);
	const uint32_t frameIndex = object3DCommon_->GetDxCommon()->GetCurrentFrameIndex();
	const uint32_t paletteSrvIndex = skinPaletteSrvIndices_[frameIndex];
	object3DCommon_->GetSrvManager()->CreateSRVforStructuredBuffer(
		paletteSrvIndex,
		paletteAllocation.resource,
		static_cast<UINT>(skinPaletteData_.size()),
		sizeof(WellForGPU),
		paletteAllocation.offset / sizeof(WellForGPU));

	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformAddress);
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(4, cameraAddress);
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootDescriptorTable(
		7,
		object3DCommon_->GetSrvManager()->GetGPUDescriptorHandle(paletteSrvIndex));
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(5, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(6, environmentAddress);
	object3DCommon_->BindSceneLighting(true);
	if (model_) {
		model_->Draw(materialAddress);
	}
}

void Object3D::DrawShadow()
{
	if (!castsShadow_ || !model_ || !object3DCommon_->IsShadowPassActive()) {
		return;
	}
	const Engine::Math::OBB bounds = GetScaledModelObb();
	const float boundingRadius = std::sqrt(
		bounds.size.x * bounds.size.x +
		bounds.size.y * bounds.size.y +
		bounds.size.z * bounds.size.z);
	const bool insideShadowFrustum = object3DCommon_->IsInsideShadowFrustum(
		bounds.center,
		boundingRadius);
	object3DCommon_->RecordShadowCandidate(insideShadowFrustum);
	if (!insideShadowFrustum) {
		return;
	}
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	object3DCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(
		0, transformAddress);
	model_->DrawGeometry();
}

void Object3D::SetModel(Model* model)
{
	model_ = model;
	InitializeSkinningState();
}

void Object3D::SetModel(const std::string& filepath)
{
	// モデルを検索してセットする
	SetModel(ModelManager::GetInstance()->FindModel(filepath));
}

void Object3D::SetModelFromResourceRoot(const std::string& resourceRoot, const std::string& filepath)
{
	SetModel(ModelManager::GetInstance()->FindModelFromResourceRoot(resourceRoot, filepath));
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
	transformationMatrixData_.WVP =
		transformationMatrixData_.WVP.MakeIdentity4x4();
	transformationMatrixData_.World =
		transformationMatrixData_.World.MakeIdentity4x4();
	transformationMatrixData_.worldInverseTranspose =
		transformationMatrixData_.worldInverseTranspose.MakeIdentity4x4();
}

void Object3D::InitializeLightResources()
{
	directionalLightData_.color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData_.direction = { 0.0f,-1.0f,1.0f };
	directionalLightData_.intensity = 1.0f;
	directionalLightData_.enable = 1;

	pointLightData_.color = { 1.0f,1.0f,1.0f,1.0f };
	pointLightData_.position = { 0.0f,0.0f,0.0f };
	pointLightData_.intensity = 1.0f;
	pointLightData_.radius = 10.0f;
	pointLightData_.decay = 1.0f;
	pointLightData_.enable = 0;

	spotLightData_.color = { 1.0f,1.0f,1.0f,1.0f };
	spotLightData_.position = { 0.0f,2.0f,0.0f };
	spotLightData_.intensity = 4.0f;
	spotLightData_.direction = MyMath::Normalize(Vector3{ 0.0f,-1.0f,0.0f });
	spotLightData_.distance = 7.0f;
	spotLightData_.decay = 2.0f;
	spotLightData_.coneAngleCos = std::cos(std::numbers::pi_v<float> / 3.0f);
	spotLightData_.cosFalloffStart = 1.0f;
	spotLightData_.enable = 0;
}

void Object3D::InitializeEnvironmentResources()
{
	environmentReflectionSettingData_.reflectionStrength = kDefaultEnvironmentReflectionStrength;
	environmentReflectionSettingData_.roughness = kDefaultEnvironmentRoughness;
	environmentReflectionSettingData_.textureInfluence = 1.0f;
	environmentReflectionSettingData_.padding = 0.0f;
}

void Object3D::InitializeMaterialResources()
{
	materialData_.color = color_;
	materialData_.enableLighting = enableLighting;
	materialData_.uvTransform = materialData_.uvTransform.MakeIdentity4x4();
	materialData_.shininess = 60.0f;
}

void Object3D::InitializeCameraResources()
{
	cameraForGpu_ = {};
}

void Object3D::InitializeSkinningState()
{
	ReleaseSkinningDescriptors();
	skeleton_ = {};
	skeletonPose_.clear();
	skinPaletteData_.clear();
	animationTime = 0.0f;

	if (!object3DCommon_ || !model_ || !model_->HasSkinningData() || model_->GetSkeleton().joints.empty()) {
		return;
	}

	static_assert(kBufferedFrameCount == Engine::Base::DirectXCommon::kFrameCount);
	skeleton_ = model_->GetSkeleton();
	SkeletonUpdate(skeleton_);
	SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
	for (uint32_t& srvIndex : skinPaletteSrvIndices_) {
		srvIndex = object3DCommon_->GetSrvManager()->Allocate();
	}
}

void Object3D::ReleaseSkinningDescriptors()
{
	if (!object3DCommon_ || !object3DCommon_->GetSrvManager()) {
		return;
	}
	for (uint32_t& srvIndex : skinPaletteSrvIndices_) {
		if (srvIndex != UINT32_MAX) {
			object3DCommon_->GetSrvManager()->Free(srvIndex);
			srvIndex = UINT32_MAX;
		}
	}
}

void Object3D::UpdateAnimationState()
{
	if (!enableAnimation_ || !model_ || model_->GetAnimation().nodeAnimations.empty()) {
		return;
	}

	ApplyAnimation(skeleton_, model_->GetAnimation(), animationTime);
	SkeletonUpdate(skeleton_);
	SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
	animationTime += kFixedAnimationDeltaTime;
	const float duration = model_->GetAnimation().duration;
	animationTime = std::isfinite(duration) && duration > 0.0f
		? std::fmod(animationTime, duration)
		: 0.0f;
}

void Object3D::ApplyModelSettings()
{
	materialData_.enableLighting = enableLighting;
	materialData_.color = color_;
}

void Object3D::UpdateTransformationMatrices()
{
	worldMatrix = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	transformationMatrixData_.World = worldMatrix;
	transformationMatrixData_.worldInverseTranspose = MyMath::Transpose(worldMatrix.Inverse());

	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		worldViewProjectionMatrix = worldMatrix;
		transformationMatrixData_.WVP = worldViewProjectionMatrix;
		return;
	}

	worldViewProjectionMatrix = worldMatrix * activeCamera->GetViewProjectionMatrix();
	transformationMatrixData_.WVP = worldViewProjectionMatrix;
	cameraForGpu_.worldPosition = activeCamera->GetTransform().translate;
}

D3D12_GPU_VIRTUAL_ADDRESS Object3D::UploadFrameConstant(
	const void* data,
	size_t size)
{
	const Engine::Base::DirectXCommon::FrameUploadAllocation allocation =
		object3DCommon_->GetDxCommon()->AllocateFrameUpload(size, 256);
	std::memcpy(allocation.cpuAddress, data, size);
	return allocation.gpuAddress;
}



Vector3 Object3D::CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time)
{
	if (keyframes.empty()) {
		return {};
	}
	if (keyframes.size() == 1 || time <= keyframes[0].time) {
		return keyframes[0].value;
	}

	for (size_t index = 0; index < keyframes.size() - 1; ++index) {
		size_t nextIndex = index + 1;
		//indexとnextIndexの2つのキーフレームを取得して範囲内に時刻があるか判定する
		if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
			const float keyframeDuration =
				keyframes[nextIndex].time - keyframes[index].time;
			if (std::abs(keyframeDuration) <= 0.000001f) {
				return keyframes[nextIndex].value;
			}
			const float t =
				(time - keyframes[index].time) / keyframeDuration;
			return MyMath::Lerp(keyframes[index].value, keyframes[nextIndex].value, t);
		}

	}
	return (*keyframes.rbegin()).value;
}

Quaternion Object3D::CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time)
{
	if (keyframes.empty()) {
		return { 0.0f, 0.0f, 0.0f, 1.0f };
	}
	if (keyframes.size() == 1 || time <= keyframes[0].time) {
		return keyframes[0].value;
	}
	for (size_t index = 0; index < keyframes.size() - 1; ++index) {
		size_t nextIndex = index + 1;
		//indexとnextIndexの2つのキーフレームを取得して範囲内に時刻があるか判定する
		if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
			const float keyframeDuration =
				keyframes[nextIndex].time - keyframes[index].time;
			if (std::abs(keyframeDuration) <= 0.000001f) {
				return keyframes[nextIndex].value;
			}
			const float t =
				(time - keyframes[index].time) / keyframeDuration;
			return MyMath::Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
		}
	}
	return (*keyframes.rbegin()).value;
}

}

