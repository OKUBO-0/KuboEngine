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
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr float kFixedAnimationDeltaTime = 1.0f / 60.0f;
constexpr float kDefaultEnvironmentReflectionStrength = 0.5f;
constexpr float kDefaultEnvironmentRoughness = 0.5f;
constexpr float kSkinPaletteCacheSampleRate = 30.0f;
constexpr size_t kMaxSkinPaletteCacheEntries = 512;

struct ObjectInstanceForGPU {
	TransformationMatrix transform;
	Vector4 color;
};

struct SkinPaletteCacheKey {
	const Engine::Graphics3D::Model* model = nullptr;
	std::string clipName;
	uint32_t sampleFrame = 0;

	bool operator==(const SkinPaletteCacheKey& other) const
	{
		return model == other.model &&
			sampleFrame == other.sampleFrame &&
			clipName == other.clipName;
	}
};

struct SkinPaletteCacheKeyHash {
	size_t operator()(const SkinPaletteCacheKey& key) const
	{
		size_t value = std::hash<const void*>{}(key.model);
		value ^= std::hash<uint32_t>{}(key.sampleFrame) + 0x9e3779b9u +
			(value << 6) + (value >> 2);
		value ^= std::hash<std::string>{}(key.clipName) + 0x9e3779b9u +
			(value << 6) + (value >> 2);
		return value;
	}
};

struct SkinPaletteCacheEntry {
	std::vector<Engine::Graphics3D::WellForGPU> palette;
	std::array<ID3D12Resource*, Engine::Base::DirectXCommon::kFrameCount> gpuUploadResources{};
	std::array<UINT64, Engine::Base::DirectXCommon::kFrameCount> gpuFirstElements{};
	std::array<uint64_t, Engine::Base::DirectXCommon::kFrameCount> gpuFrameSerials{};
};

std::unordered_map<SkinPaletteCacheKey, SkinPaletteCacheEntry, SkinPaletteCacheKeyHash>
	gSkinPaletteCache;
std::vector<Engine::Graphics3D::Object3D*> gSubmittedDrawObjects;
std::vector<Engine::Graphics3D::Object3D*> gSubmittedShadowObjects;

std::string_view StripSkinPrefix(std::string_view jointName)
{
	const size_t separator = jointName.find("::");
	if (separator == std::string_view::npos) {
		return jointName;
	}
	return jointName.substr(separator + 2);
}

uint32_t QuantizeAnimationFrame(float animationTime)
{
	const float safeTime = std::isfinite(animationTime)
		? (std::max)(0.0f, animationTime)
		: 0.0f;
	return static_cast<uint32_t>(safeTime * kSkinPaletteCacheSampleRate);
}

void AdvanceAnimationTime(
	float& animationTime,
	const Animation& animation,
	float animationSpeed,
	bool animationLoop)
{
	animationTime += kFixedAnimationDeltaTime *
		(std::max)(0.0f, animationSpeed);
	const float duration = animation.duration;
	if (std::isfinite(duration) && duration > 0.0f) {
		animationTime = animationLoop
			? std::fmod(animationTime, duration)
			: (std::min)(animationTime, duration);
	} else {
		animationTime = 0.0f;
	}
}
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
		const std::string animationJointName{ StripSkinPrefix(joint.name) };
		if (auto it = animation.nodeAnimations.find(animationJointName); it != animation.nodeAnimations.end()) {
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
		// inverseBindPose は「メッシュのバインド姿勢」から各Joint空間へ戻す行列。
		// そこへ現在フレームの skeletonSpaceMatrix を掛け、頂点を現在姿勢のSkeleton空間へ移す。
		// この順序にすることで、バインド時との差分変形だけがスキニングへ反映される。
		skinPaletteData_[jointIndex].skeletonSpaceMatrix =
			skinCluster.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
		// 法線は平行移動を含む通常行列ではなく、変形行列の逆転置で変換する。
		// 非一様スケールが入ってもライティング用法線の向きが破綻しにくくなる。
		skinPaletteData_[jointIndex].skeletonSpaceInverseTransposeMatrix =
			MyMath::Transpose(skinPaletteData_[jointIndex].skeletonSpaceMatrix.Inverse());
	}
}




void Object3D::Draw()
{
	const bool insideCameraFrustum = IsInsideActiveCameraFrustum();
	object3DCommon_->RecordDrawCandidate(insideCameraFrustum);
	if (!insideCameraFrustum) {
		return;
	}
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
		UploadFrameConstant(&cameraForGpu_, sizeof(cameraForGpu_));
	const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
		UploadFrameConstant(&environmentReflectionSettingData_, sizeof(environmentReflectionSettingData_));
	const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
		UploadFrameConstant(&materialData_, sizeof(materialData_));

	commandList->SetGraphicsRootConstantBufferView(1, transformAddress);
	commandList->SetGraphicsRootConstantBufferView(4, cameraAddress);
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(5, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	commandList->SetGraphicsRootConstantBufferView(6, environmentAddress);
	object3DCommon_->BindSceneLighting();
	if (model_) {
		model_->Draw(materialAddress, &materialData_);
	}
}

void Object3D::DrawAnimated()
{
	if (!CanDrawSkinning()) {
		Draw();
		return;
	}
	object3DCommon_->SkinningCommonDraw();
	DrawSkinning();
	object3DCommon_->CommonDraw();
}

void Object3D::DrawSkinning()
{
	if (!model_ || skinPaletteData_.empty()) {
		return;
	}
	const bool insideCameraFrustum = IsInsideActiveCameraFrustum();
	object3DCommon_->RecordDrawCandidate(insideCameraFrustum);
	if (!insideCameraFrustum) {
		return;
	}

	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
		UploadFrameConstant(&cameraForGpu_, sizeof(cameraForGpu_));
	const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
		UploadFrameConstant(&environmentReflectionSettingData_, sizeof(environmentReflectionSettingData_));
	const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
		UploadFrameConstant(&materialData_, sizeof(materialData_));
	const uint32_t paletteSrvIndex = ResolveSkinPaletteSrvIndex();

	commandList->SetGraphicsRootConstantBufferView(1, transformAddress);
	commandList->SetGraphicsRootConstantBufferView(4, cameraAddress);
	commandList->SetGraphicsRootDescriptorTable(
		7,
		object3DCommon_->GetSrvManager()->GetGPUDescriptorHandle(paletteSrvIndex));
	object3DCommon_->GetSrvManager()->SetGraphicsRootDescriptorTable(5, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(skyboxFilePath_));
	commandList->SetGraphicsRootConstantBufferView(6, environmentAddress);
	object3DCommon_->BindSceneLighting(true);
	if (model_) {
		model_->Draw(materialAddress, &materialData_);
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
	if (CanDrawSkinning()) {
		object3DCommon_->SkinningShadowCommonDraw();
		DrawSkinningShadow();
		return;
	}
	object3DCommon_->ShadowCommonDraw();
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(&transformationMatrixData_, sizeof(transformationMatrixData_));
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(0, transformAddress);
	model_->DrawGeometry();
}

void Object3D::DrawSkinningShadow()
{
	if (!model_ || skinPaletteData_.empty()) {
		return;
	}

	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
		UploadFrameConstant(
			&transformationMatrixData_,
			sizeof(transformationMatrixData_));
	const uint32_t paletteSrvIndex = ResolveSkinPaletteSrvIndex();

	commandList->SetGraphicsRootConstantBufferView(1, transformAddress);
	commandList->SetGraphicsRootDescriptorTable(
		7,
		object3DCommon_->GetSrvManager()->GetGPUDescriptorHandle(
			paletteSrvIndex));
	model_->DrawSkinnedGeometry();
}

void Object3D::SubmitForDraw(Object3D* object)
{
	if (!object) {
		return;
	}
	gSubmittedDrawObjects.push_back(object);
}

void Object3D::FlushSubmittedDraws()
{
	if (gSubmittedDrawObjects.empty()) {
		return;
	}
	std::vector<Object3D*> staticObjects;
	std::vector<Object3D*> skinnedObjects;
	staticObjects.reserve(gSubmittedDrawObjects.size());
	skinnedObjects.reserve(gSubmittedDrawObjects.size());
	for (Object3D* object : gSubmittedDrawObjects) {
		if (!object || !object->model_) {
			continue;
		}
		if (object->CanDrawSkinning()) {
			skinnedObjects.push_back(object);
		} else {
			staticObjects.push_back(object);
		}
	}
	DrawStaticBatch(staticObjects);
	DrawSkinningBatch(skinnedObjects);
	if (Object3DCommon* common = Object3DCommon::GetInstance()) {
		common->RecordRenderQueueFlush(
			static_cast<uint32_t>(gSubmittedDrawObjects.size()),
			static_cast<uint32_t>(staticObjects.size()),
			static_cast<uint32_t>(skinnedObjects.size()));
	}
	gSubmittedDrawObjects.clear();
}

void Object3D::ClearSubmittedDraws()
{
	gSubmittedDrawObjects.clear();
}

void Object3D::SubmitForShadow(Object3D* object)
{
	if (!object) {
		return;
	}
	gSubmittedShadowObjects.push_back(object);
}

void Object3D::FlushSubmittedShadows()
{
	if (gSubmittedShadowObjects.empty()) {
		return;
	}
	std::vector<Object3D*> staticObjects;
	std::vector<Object3D*> skinnedObjects;
	staticObjects.reserve(gSubmittedShadowObjects.size());
	skinnedObjects.reserve(gSubmittedShadowObjects.size());
	for (Object3D* object : gSubmittedShadowObjects) {
		if (!object || !object->model_) {
			continue;
		}
		if (object->CanDrawSkinning()) {
			skinnedObjects.push_back(object);
		} else {
			staticObjects.push_back(object);
		}
	}
	if (Object3DCommon* common = Object3DCommon::GetInstance()) {
		common->RecordShadowQueueFlush(
			static_cast<uint32_t>(gSubmittedShadowObjects.size()));
	}
	DrawStaticShadowBatch(staticObjects);
	DrawSkinningShadowBatch(skinnedObjects);
	gSubmittedShadowObjects.clear();
}

void Object3D::ClearSubmittedShadows()
{
	gSubmittedShadowObjects.clear();
}

void Object3D::DrawSkinningBatch(const std::vector<Object3D*>& objects)
{
	struct BatchKey {
		Object3DCommon* common = nullptr;
		Model* model = nullptr;
		std::string clipName;
		uint32_t sampleFrame = 0;
		int32_t enableLighting = 0;

		bool operator==(const BatchKey& other) const
		{
			return common == other.common &&
				model == other.model &&
				sampleFrame == other.sampleFrame &&
				clipName == other.clipName &&
				enableLighting == other.enableLighting;
		}
	};
	struct BatchKeyHash {
		size_t operator()(const BatchKey& key) const
		{
			size_t value = std::hash<const void*>{}(key.common);
			value ^= std::hash<const void*>{}(key.model) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<std::string>{}(key.clipName) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<uint32_t>{}(key.sampleFrame) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<int32_t>{}(key.enableLighting) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			return value;
		}
	};
	struct Batch {
		Object3D* representative = nullptr;
		std::vector<Object3D*> objects;
		std::vector<ObjectInstanceForGPU> instances;
	};

	std::unordered_map<BatchKey, Batch, BatchKeyHash> batches;
	std::vector<Object3D*> fallbackObjects;
	for (Object3D* object : objects) {
		if (!object || !object->CanDrawSkinning()) {
			continue;
		}
		const bool insideCameraFrustum = object->IsInsideActiveCameraFrustum();
		object->object3DCommon_->RecordDrawCandidate(insideCameraFrustum);
		if (!insideCameraFrustum) {
			continue;
		}
		if (!object->skinPaletteCacheKeyValid_) {
			fallbackObjects.push_back(object);
			continue;
		}
		const BatchKey key{
			object->object3DCommon_,
			object->model_,
			object->skinPaletteCacheClipName_,
			object->skinPaletteCacheSampleFrame_,
			object->materialData_.enableLighting,
		};
		Batch& batch = batches[key];
		if (!batch.representative) {
			batch.representative = object;
		}
		batch.objects.push_back(object);
		batch.instances.push_back({
			object->transformationMatrixData_,
			object->materialData_.color,
			});
	}

	for (Object3D* object : fallbackObjects) {
		object->DrawSkinning();
	}
	for (auto& [key, batch] : batches) {
		Object3D* object = batch.representative;
		if (!object || batch.instances.empty()) {
			continue;
		}
		Object3DCommon* common = object->object3DCommon_;
		common->SkinningInstancingCommonDraw();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			common->GetDxCommon();
		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
		const size_t instanceBytes =
			sizeof(ObjectInstanceForGPU) * batch.instances.size();
		const Engine::Base::DirectXCommon::FrameUploadAllocation instanceAllocation =
			dxCommon->AllocateFrameUpload(instanceBytes, sizeof(ObjectInstanceForGPU));
		std::memcpy(
			instanceAllocation.cpuAddress,
			batch.instances.data(),
			instanceBytes);
		const uint32_t instanceSrvIndex = common->BindSkinningInstanceTransforms(
			instanceAllocation.resource,
			static_cast<UINT>(batch.instances.size()),
			instanceAllocation.offset,
			sizeof(ObjectInstanceForGPU));
		if (instanceSrvIndex == UINT32_MAX) {
			for (Object3D* batchObject : batch.objects) {
				if (batchObject) {
					batchObject->DrawSkinning();
				}
			}
			continue;
		}
		common->RecordSkinningInstanceBatch(
			static_cast<uint32_t>(batch.instances.size()));

		const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
			object->UploadFrameConstant(
				&object->transformationMatrixData_,
				sizeof(object->transformationMatrixData_));
		const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
			object->UploadFrameConstant(
				&object->cameraForGpu_,
				sizeof(object->cameraForGpu_));
		const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
			object->UploadFrameConstant(
				&object->environmentReflectionSettingData_,
				sizeof(object->environmentReflectionSettingData_));
		Material batchMaterial = object->materialData_;
		batchMaterial.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
			object->UploadFrameConstant(
				&batchMaterial,
				sizeof(batchMaterial));
		const uint32_t paletteSrvIndex = object->ResolveSkinPaletteSrvIndex();

		commandList->SetGraphicsRootConstantBufferView(1, transformAddress);
		commandList->SetGraphicsRootConstantBufferView(4, cameraAddress);
		commandList->SetGraphicsRootDescriptorTable(
			7,
			common->GetSrvManager()->GetGPUDescriptorHandle(paletteSrvIndex));
		common->GetSrvManager()->SetGraphicsRootDescriptorTable(
			5,
			Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(
				object->skyboxFilePath_));
		commandList->SetGraphicsRootConstantBufferView(6, environmentAddress);
		common->BindSceneLighting(true);
		object->model_->DrawInstanced(
			static_cast<UINT>(batch.instances.size()),
			materialAddress,
			&batchMaterial);
	}
	Object3DCommon::GetInstance()->CommonDraw();
}

void Object3D::DrawStaticBatch(const std::vector<Object3D*>& objects)
{
	struct BatchKey {
		Object3DCommon* common = nullptr;
		Model* model = nullptr;
		std::string skyboxFilePath;
		int32_t enableLighting = 0;
		float reflectionStrength = 0.0f;
		float roughness = 0.0f;
		float textureInfluence = 0.0f;

		bool operator==(const BatchKey& other) const
		{
			return common == other.common &&
				model == other.model &&
				skyboxFilePath == other.skyboxFilePath &&
				enableLighting == other.enableLighting &&
				reflectionStrength == other.reflectionStrength &&
				roughness == other.roughness &&
				textureInfluence == other.textureInfluence;
		}
	};
	struct BatchKeyHash {
		size_t operator()(const BatchKey& key) const
		{
			size_t value = std::hash<const void*>{}(key.common);
			value ^= std::hash<const void*>{}(key.model) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<std::string>{}(key.skyboxFilePath) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<int32_t>{}(key.enableLighting) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<float>{}(key.reflectionStrength) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<float>{}(key.roughness) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<float>{}(key.textureInfluence) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			return value;
		}
	};
	struct Batch {
		Object3D* representative = nullptr;
		std::vector<ObjectInstanceForGPU> instances;
	};

	std::unordered_map<BatchKey, Batch, BatchKeyHash> batches;
	for (Object3D* object : objects) {
		if (!object || !object->model_ || object->CanDrawSkinning()) {
			continue;
		}
		const bool insideCameraFrustum = object->IsInsideActiveCameraFrustum();
		object->object3DCommon_->RecordDrawCandidate(insideCameraFrustum);
		if (!insideCameraFrustum) {
			continue;
		}
		const BatchKey key{
			object->object3DCommon_,
			object->model_,
			object->skyboxFilePath_,
			object->materialData_.enableLighting,
			object->environmentReflectionSettingData_.reflectionStrength,
			object->environmentReflectionSettingData_.roughness,
			object->environmentReflectionSettingData_.textureInfluence,
		};
		Batch& batch = batches[key];
		if (!batch.representative) {
			batch.representative = object;
		}
		batch.instances.push_back({
			object->transformationMatrixData_,
			object->materialData_.color,
			});
	}

	for (auto& [key, batch] : batches) {
		Object3D* object = batch.representative;
		if (!object || batch.instances.empty()) {
			continue;
		}
		Object3DCommon* common = object->object3DCommon_;
		common->ObjectInstancingCommonDraw();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			common->GetDxCommon();
		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
		const size_t instanceBytes =
			sizeof(ObjectInstanceForGPU) * batch.instances.size();
		const Engine::Base::DirectXCommon::FrameUploadAllocation instanceAllocation =
			dxCommon->AllocateFrameUpload(instanceBytes, sizeof(ObjectInstanceForGPU));
		std::memcpy(
			instanceAllocation.cpuAddress,
			batch.instances.data(),
			instanceBytes);
		const uint32_t instanceSrvIndex = common->BindObjectInstanceData(
			instanceAllocation.resource,
			static_cast<UINT>(batch.instances.size()),
			instanceAllocation.offset,
			sizeof(ObjectInstanceForGPU));
		if (instanceSrvIndex == UINT32_MAX) {
			for (Object3D* fallback : objects) {
				if (fallback && fallback->model_ == object->model_) {
					fallback->Draw();
				}
			}
			continue;
		}
		common->RecordObjectInstanceBatch(
			static_cast<uint32_t>(batch.instances.size()));

		const D3D12_GPU_VIRTUAL_ADDRESS transformAddress =
			object->UploadFrameConstant(
				&object->transformationMatrixData_,
				sizeof(object->transformationMatrixData_));
		const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress =
			object->UploadFrameConstant(
				&object->cameraForGpu_,
				sizeof(object->cameraForGpu_));
		const D3D12_GPU_VIRTUAL_ADDRESS environmentAddress =
			object->UploadFrameConstant(
				&object->environmentReflectionSettingData_,
				sizeof(object->environmentReflectionSettingData_));
		Material batchMaterial = object->materialData_;
		batchMaterial.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		const D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
			object->UploadFrameConstant(&batchMaterial, sizeof(batchMaterial));

		commandList->SetGraphicsRootConstantBufferView(1, transformAddress);
		commandList->SetGraphicsRootConstantBufferView(4, cameraAddress);
		common->GetSrvManager()->SetGraphicsRootDescriptorTable(
			5,
			Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(
				object->skyboxFilePath_));
		commandList->SetGraphicsRootConstantBufferView(6, environmentAddress);
		common->BindSceneLighting(false);
		object->model_->DrawInstanced(
			static_cast<UINT>(batch.instances.size()),
			materialAddress,
			&batchMaterial);
	}
	Object3DCommon::GetInstance()->CommonDraw();
}

void Object3D::DrawStaticShadowBatch(const std::vector<Object3D*>& objects)
{
	struct Batch {
		Object3D* representative = nullptr;
		std::vector<ObjectInstanceForGPU> instances;
	};

	std::unordered_map<Model*, Batch> batches;
	for (Object3D* object : objects) {
		if (!object || !object->model_ || object->CanDrawSkinning()) {
			continue;
		}
		if (!object->castsShadow_) {
			continue;
		}
		const float boundingRadius = object->GetScaledModelBoundingRadius(1.0f);
		const Vector3 worldCenter{
			object->transformationMatrixData_.World.m[3][0],
			object->transformationMatrixData_.World.m[3][1],
			object->transformationMatrixData_.World.m[3][2],
		};
		const bool insideShadowFrustum =
			object->object3DCommon_->IsInsideShadowFrustum(
				worldCenter,
				boundingRadius);
		object->object3DCommon_->RecordShadowCandidate(insideShadowFrustum);
		if (!insideShadowFrustum) {
			continue;
		}
		Batch& batch = batches[object->model_];
		if (!batch.representative) {
			batch.representative = object;
		}
		batch.instances.push_back({
			object->transformationMatrixData_,
			object->materialData_.color,
			});
	}

	for (auto& [model, batch] : batches) {
		Object3D* object = batch.representative;
		if (!object || !model || batch.instances.empty()) {
			continue;
		}
		Object3DCommon* common = object->object3DCommon_;
		common->ObjectInstancingShadowCommonDraw();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			common->GetDxCommon();
		const size_t instanceBytes =
			sizeof(ObjectInstanceForGPU) * batch.instances.size();
		const Engine::Base::DirectXCommon::FrameUploadAllocation instanceAllocation =
			dxCommon->AllocateFrameUpload(instanceBytes, sizeof(ObjectInstanceForGPU));
		std::memcpy(
			instanceAllocation.cpuAddress,
			batch.instances.data(),
			instanceBytes);
		const uint32_t instanceSrvIndex = common->BindObjectInstanceData(
			instanceAllocation.resource,
			static_cast<UINT>(batch.instances.size()),
			instanceAllocation.offset,
			sizeof(ObjectInstanceForGPU));
		if (instanceSrvIndex == UINT32_MAX) {
			for (Object3D* fallback : objects) {
				if (fallback && fallback->model_ == model) {
					fallback->DrawShadow();
				}
			}
			continue;
		}
		common->RecordObjectInstanceBatch(
			static_cast<uint32_t>(batch.instances.size()));
		model->DrawGeometryInstanced(
			static_cast<UINT>(batch.instances.size()));
	}
}

void Object3D::DrawSkinningShadowBatch(const std::vector<Object3D*>& objects)
{
	struct BatchKey {
		Object3DCommon* common = nullptr;
		Model* model = nullptr;
		std::string clipName;
		uint32_t sampleFrame = 0;

		bool operator==(const BatchKey& other) const
		{
			return common == other.common &&
				model == other.model &&
				sampleFrame == other.sampleFrame &&
				clipName == other.clipName;
		}
	};
	struct BatchKeyHash {
		size_t operator()(const BatchKey& key) const
		{
			size_t value = std::hash<const void*>{}(key.common);
			value ^= std::hash<const void*>{}(key.model) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<std::string>{}(key.clipName) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			value ^= std::hash<uint32_t>{}(key.sampleFrame) + 0x9e3779b9u +
				(value << 6) + (value >> 2);
			return value;
		}
	};
	struct Batch {
		Object3D* representative = nullptr;
		std::vector<Object3D*> objects;
		std::vector<TransformationMatrix> instances;
	};

	std::unordered_map<BatchKey, Batch, BatchKeyHash> batches;
	std::vector<Object3D*> fallbackObjects;
	for (Object3D* object : objects) {
		if (!object || !object->CanDrawSkinning()) {
			continue;
		}
		if (!object->castsShadow_) {
			continue;
		}
		const float boundingRadius = object->GetScaledModelBoundingRadius(1.0f);
		const Vector3 worldCenter{
			object->transformationMatrixData_.World.m[3][0],
			object->transformationMatrixData_.World.m[3][1],
			object->transformationMatrixData_.World.m[3][2],
		};
		const bool insideShadowFrustum =
			object->object3DCommon_->IsInsideShadowFrustum(
				worldCenter,
				boundingRadius);
		object->object3DCommon_->RecordShadowCandidate(insideShadowFrustum);
		if (!insideShadowFrustum) {
			continue;
		}
		if (!object->skinPaletteCacheKeyValid_) {
			fallbackObjects.push_back(object);
			continue;
		}
		const BatchKey key{
			object->object3DCommon_,
			object->model_,
			object->skinPaletteCacheClipName_,
			object->skinPaletteCacheSampleFrame_,
		};
		Batch& batch = batches[key];
		if (!batch.representative) {
			batch.representative = object;
		}
		batch.objects.push_back(object);
		batch.instances.push_back(object->transformationMatrixData_);
	}

	for (Object3D* object : fallbackObjects) {
		object->DrawShadow();
	}
	for (auto& [key, batch] : batches) {
		Object3D* object = batch.representative;
		if (!object || batch.instances.empty()) {
			continue;
		}
		Object3DCommon* common = object->object3DCommon_;
		common->SkinningInstancingShadowCommonDraw();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			common->GetDxCommon();
		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
		const size_t instanceBytes =
			sizeof(TransformationMatrix) * batch.instances.size();
		const Engine::Base::DirectXCommon::FrameUploadAllocation instanceAllocation =
			dxCommon->AllocateFrameUpload(instanceBytes, sizeof(TransformationMatrix));
		std::memcpy(
			instanceAllocation.cpuAddress,
			batch.instances.data(),
			instanceBytes);
		const uint32_t instanceSrvIndex = common->BindSkinningInstanceTransforms(
			instanceAllocation.resource,
			static_cast<UINT>(batch.instances.size()),
			instanceAllocation.offset,
			sizeof(TransformationMatrix));
		if (instanceSrvIndex == UINT32_MAX) {
			for (Object3D* batchObject : batch.objects) {
				if (batchObject) {
					batchObject->DrawShadow();
				}
			}
			continue;
		}
		common->RecordSkinningInstanceBatch(
			static_cast<uint32_t>(batch.instances.size()));
		const uint32_t paletteSrvIndex = object->ResolveSkinPaletteSrvIndex();
		commandList->SetGraphicsRootDescriptorTable(
			7,
			common->GetSrvManager()->GetGPUDescriptorHandle(paletteSrvIndex));
		object->model_->DrawSkinnedGeometryInstanced(
			static_cast<UINT>(batch.instances.size()));
	}
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

void Object3D::SetAnimationClip(const std::string& clipName)
{
	if (animationClipName_ == clipName) {
		return;
	}
	animationClipName_ = clipName;
	animationTime = 0.0f;
	animationUpdateFrame_ = 0;
	if (model_ && model_->FindAnimationClip(animationClipName_)) {
		skeleton_ = model_->GetSkeleton();
		SkeletonUpdate(skeleton_);
		SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
	}
}

void Object3D::SetAnimationUpdateStride(uint32_t stride)
{
	animationUpdateStride_ = (std::max)(1u, stride);
}

bool Object3D::HasAnimationClip(const std::string& clipName) const
{
	return model_ && model_->FindAnimationClip(clipName) != nullptr;
}

bool Object3D::CanDrawSkinning() const
{
	return model_ &&
		model_->HasSkinningData() &&
		(model_->FindAnimationClip(animationClipName_) != nullptr ||
			model_->FindAnimationClip("default") != nullptr) &&
		!skinPaletteData_.empty();
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

bool Object3D::IsInsideActiveCameraFrustum() const
{
	if (!frustumCullingEnabled_ || !model_) {
		return true;
	}
	Engine::CameraSystem::Camera* activeCamera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		return true;
	}

	const Engine::Math::OBB bounds = GetScaledModelObb();
	const float radius = std::sqrt(
		bounds.size.x * bounds.size.x +
		bounds.size.y * bounds.size.y +
		bounds.size.z * bounds.size.z);
	const Vector3 clipCenter =
		MyMath::Transform(bounds.center, activeCamera->GetViewProjectionMatrix());
	const Vector3 viewCenter =
		MyMath::Transform(bounds.center, activeCamera->GetViewMatrix());
	const float depth = (std::max)(1.0f, std::abs(viewCenter.z));
	const float margin = std::clamp(radius / depth * 2.2f, 0.08f, 1.25f);
	return clipCenter.x >= -1.0f - margin &&
		clipCenter.x <= 1.0f + margin &&
		clipCenter.y >= -1.0f - margin &&
		clipCenter.y <= 1.0f + margin &&
		clipCenter.z >= -0.18f &&
		clipCenter.z <= 1.18f;
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
	skinPaletteCacheKeyValid_ = false;
	skinPaletteCacheClipName_.clear();
	skinPaletteCacheSampleFrame_ = 0;
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
		object3DCommon_->GetSrvManager()->LabelUsage(srvIndex, "SkinPalette");
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
	if (!enableAnimation_ || !model_) {
		skinPaletteCacheKeyValid_ = false;
		return;
	}
	const Animation* animation = model_->FindAnimationClip(animationClipName_);
	std::string resolvedClipName = animationClipName_;
	if (!animation) {
		animation = model_->FindAnimationClip("default");
		resolvedClipName = "default";
	}
	if (!animation || animation->nodeAnimations.empty()) {
		skinPaletteCacheKeyValid_ = false;
		return;
	}

	const uint32_t stride = (std::max)(1u, animationUpdateStride_);
	const bool shouldSample = stride == 1 ||
		(animationUpdateFrame_++ % stride) == 0;
	if (!shouldSample) {
		AdvanceAnimationTime(
			animationTime,
			*animation,
			animationSpeed_,
			animationLoop_);
		return;
	}

	const SkinPaletteCacheKey cacheKey{
		model_,
		resolvedClipName,
		QuantizeAnimationFrame(animationTime),
	};
	const auto cacheIt = gSkinPaletteCache.find(cacheKey);
	if (cacheIt != gSkinPaletteCache.end()) {
		skinPaletteData_ = cacheIt->second.palette;
		skinPaletteCacheKeyValid_ = true;
		skinPaletteCacheClipName_ = cacheKey.clipName;
		skinPaletteCacheSampleFrame_ = cacheKey.sampleFrame;
		if (object3DCommon_) {
			object3DCommon_->RecordSkinningCacheHit();
		}
		AdvanceAnimationTime(
			animationTime,
			*animation,
			animationSpeed_,
			animationLoop_);
		return;
	}

	ApplyAnimation(skeleton_, *animation, animationTime);
	SkeletonUpdate(skeleton_);
	SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
	if (object3DCommon_) {
		object3DCommon_->RecordSkinningCacheMiss();
	}
	if (gSkinPaletteCache.size() >= kMaxSkinPaletteCacheEntries) {
		gSkinPaletteCache.clear();
	}
	gSkinPaletteCache.emplace(
		cacheKey,
		SkinPaletteCacheEntry{ skinPaletteData_ });
	skinPaletteCacheKeyValid_ = true;
	skinPaletteCacheClipName_ = cacheKey.clipName;
	skinPaletteCacheSampleFrame_ = cacheKey.sampleFrame;
	AdvanceAnimationTime(
		animationTime,
		*animation,
		animationSpeed_,
		animationLoop_);
}

uint32_t Object3D::ResolveSkinPaletteSrvIndex()
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	const uint32_t frameIndex = dxCommon->GetCurrentFrameIndex();
	const uint32_t paletteSrvIndex = skinPaletteSrvIndices_[frameIndex];
	const size_t paletteBytes = sizeof(WellForGPU) * skinPaletteData_.size();
	const uint64_t frameSerial = dxCommon->GetPendingSubmissionFenceValue();

	if (skinPaletteCacheKeyValid_ && model_) {
		const SkinPaletteCacheKey cacheKey{
			model_,
			skinPaletteCacheClipName_,
			skinPaletteCacheSampleFrame_,
		};
		auto cacheIt = gSkinPaletteCache.find(cacheKey);
		if (cacheIt != gSkinPaletteCache.end() &&
			cacheIt->second.palette.size() == skinPaletteData_.size()) {
			SkinPaletteCacheEntry& cacheEntry = cacheIt->second;
			if (cacheEntry.gpuFrameSerials[frameIndex] != frameSerial ||
				!cacheEntry.gpuUploadResources[frameIndex]) {
				const Engine::Base::DirectXCommon::FrameUploadAllocation paletteAllocation =
					dxCommon->AllocateFrameUpload(paletteBytes, sizeof(WellForGPU));
				std::memcpy(
					paletteAllocation.cpuAddress,
					cacheEntry.palette.data(),
					paletteBytes);
				cacheEntry.gpuUploadResources[frameIndex] = paletteAllocation.resource;
				cacheEntry.gpuFirstElements[frameIndex] =
					paletteAllocation.offset / sizeof(WellForGPU);
				cacheEntry.gpuFrameSerials[frameIndex] = frameSerial;
				if (object3DCommon_) {
					object3DCommon_->RecordSkinningGpuUploadCacheMiss();
				}
			} else if (object3DCommon_) {
				object3DCommon_->RecordSkinningGpuUploadCacheHit();
			}

			object3DCommon_->GetSrvManager()->CreateSRVforStructuredBuffer(
				paletteSrvIndex,
				cacheEntry.gpuUploadResources[frameIndex],
				static_cast<UINT>(cacheEntry.palette.size()),
				sizeof(WellForGPU),
				cacheEntry.gpuFirstElements[frameIndex]);
			return paletteSrvIndex;
		}
	}

	const Engine::Base::DirectXCommon::FrameUploadAllocation paletteAllocation =
		dxCommon->AllocateFrameUpload(paletteBytes, sizeof(WellForGPU));
	std::memcpy(
		paletteAllocation.cpuAddress,
		skinPaletteData_.data(),
		paletteBytes);
	object3DCommon_->GetSrvManager()->CreateSRVforStructuredBuffer(
		paletteSrvIndex,
		paletteAllocation.resource,
		static_cast<UINT>(skinPaletteData_.size()),
		sizeof(WellForGPU),
		paletteAllocation.offset / sizeof(WellForGPU));
	if (object3DCommon_) {
		object3DCommon_->RecordSkinningGpuUploadCacheMiss();
	}
	return paletteSrvIndex;
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
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		object3DCommon_->GetDxCommon();
	const Engine::Base::DirectXCommon::FrameUploadAllocation allocation =
		dxCommon->AllocateFrameUpload(size, 256);
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

