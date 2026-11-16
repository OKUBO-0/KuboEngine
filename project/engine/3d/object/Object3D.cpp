#include "Object3DCommon.h"
#include "Object3D.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "Model.h"
#include "MyMath.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "AnimationTime.h"
#include "engine/animation/AnimationPose.h"
#include "engine/assets/AnimatorControllerAsset.h"
#include "CameraManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using Engine::Graphics3D::SkinCluster;
using Engine::Graphics3D::WellForGPU;
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

bool FiniteMatrix(const Matrix4x4& matrix) {
	for(const auto& row:matrix.m)for(float value:row)if(!std::isfinite(value))return false;
	return true;
}
bool PrepareLayerPalette(const Skeleton& bind,const SkinCluster& skin,const std::vector<QuaternionTransform>& pose,
	Skeleton& output,std::vector<Matrix4x4>& matrices,std::vector<WellForGPU>& palette) {
	if(pose.size()!=bind.joints.size() || skin.inverseBindPoseMatrices.size()<pose.size())return false;
	Skeleton candidate=bind;std::vector<Matrix4x4> nextMatrices(pose.size());std::vector<WellForGPU> nextPalette(pose.size());
	for(size_t i=0;i<pose.size();++i) {
		auto& joint=candidate.joints[i];
		if(joint.index!=static_cast<int32_t>(i) || (joint.parent && (*joint.parent<0 || size_t(*joint.parent)>=i)))return false;
		joint.transform=pose[i];joint.localMatrix=MyMath::MakeAffineMatrix(pose[i].scale,pose[i].rotate,pose[i].translate);
		joint.skeletonSpaceMatrix=joint.parent?joint.localMatrix*candidate.joints[*joint.parent].skeletonSpaceMatrix:joint.localMatrix;
		if(!FiniteMatrix(joint.localMatrix) || !FiniteMatrix(joint.skeletonSpaceMatrix) || !FiniteMatrix(skin.inverseBindPoseMatrices[i]))return false;
		nextMatrices[i]=joint.skeletonSpaceMatrix;
		nextPalette[i].skeletonSpaceMatrix=skin.inverseBindPoseMatrices[i]*joint.skeletonSpaceMatrix;
		if(!FiniteMatrix(nextPalette[i].skeletonSpaceMatrix))return false;
		nextPalette[i].skeletonSpaceInverseTransposeMatrix=MyMath::Transpose(nextPalette[i].skeletonSpaceMatrix.Inverse());
		if(!FiniteMatrix(nextPalette[i].skeletonSpaceInverseTransposeMatrix))return false;
	}
	std::swap(output,candidate);matrices.swap(nextMatrices);palette.swap(nextPalette);return true;
}

void AdvanceAnimationTime(
	float& animationTime,
	const Animation& animation,
	float animationSpeed,
	bool animationLoop,
	float deltaTime, double* normalizedTime = nullptr)
{
	animationTime = Engine::Graphics3D::AdvanceAnimationSeconds(
		animationTime, animation.duration, deltaTime, animationSpeed, animationLoop);
	if (normalizedTime) *normalizedTime = Engine::Graphics3D::AdvanceControllerNormalizedTime(
		*normalizedTime, animationTime, animation.duration, deltaTime, animationSpeed, animationLoop);
}
}

namespace Engine::Graphics3D {

Object3D::~Object3D()
{
	ReleaseSkinningDescriptors();
	gSubmittedDrawObjects.erase(std::remove(gSubmittedDrawObjects.begin(), gSubmittedDrawObjects.end(), this), gSubmittedDrawObjects.end());
	gSubmittedShadowObjects.erase(std::remove(gSubmittedShadowObjects.begin(), gSubmittedShadowObjects.end(), this), gSubmittedShadowObjects.end());
}
void Object3D::InvalidateModelCaches(const Model* model)
{
	for (auto it = gSkinPaletteCache.begin(); it != gSkinPaletteCache.end();) {
		if (it->first.model == model) it = gSkinPaletteCache.erase(it); else ++it;
	}
}

std::unique_ptr<Object3D> Object3D::Create(Object3DCommon* common,
	const std::string& filePath, const std::string& resourceRoot, const std::string& assetIdentity, const std::string& revision)
{
	if (!common) {
		throw std::invalid_argument("Object3D::Create requires rendering common services");
	}
	auto model = ModelManager::GetInstance()->AcquireModel(filePath, resourceRoot, assetIdentity, revision);
	auto object = std::make_unique<Object3D>();
	object->Initialize(common);
	object->SetModelLease(std::move(model));
	return object;
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
	Update(kFixedAnimationDeltaTime);
}

void Object3D::Update(float deltaTime)
{
	if(animationController_ && animationLayers_) UpdateLayeredController(deltaTime);
	else {
	if (animationController_ && std::isfinite(deltaTime) && deltaTime > 0 &&
		std::isfinite(animationSpeed_) && animationSpeed_ > 0) {
		const double observed=animationNormalizedTime_;
		const bool changed=animationController_->Advance(0,controllerObservedTime_,observed,
			[this](const auto& state,const auto& transition) {
				struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(controllerTransition_);
				if (!BlendToAnimationClip(state.clip,transition.blendSeconds)) return false;
				SetAnimationSpeed(state.speed*animationControllerRate_);SetAnimationLoop(state.loop);return true;
			});
		controllerObservedTime_=changed ? 0 : observed;
	}
	UpdateAnimationState(deltaTime);
	}
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
		std::string skyboxFilePath;
		float shininess=60,reflectionStrength=0,roughness=0,textureInfluence=1;

		bool operator==(const BatchKey& other) const
		{
			return common == other.common &&
				model == other.model &&
				sampleFrame == other.sampleFrame &&
				clipName == other.clipName &&
				enableLighting == other.enableLighting && skyboxFilePath==other.skyboxFilePath &&
                shininess==other.shininess && reflectionStrength==other.reflectionStrength && roughness==other.roughness && textureInfluence==other.textureInfluence;
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
			for(const auto n:{key.shininess,key.reflectionStrength,key.roughness,key.textureInfluence})value^=std::hash<float>{}(n)+0x9e3779b9u+(value<<6)+(value>>2);
            value^=std::hash<std::string>{}(key.skyboxFilePath)+0x9e3779b9u+(value<<6)+(value>>2);
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
            object->skyboxFilePath_,object->materialData_.shininess,
            object->environmentReflectionSettingData_.reflectionStrength,
            object->environmentReflectionSettingData_.roughness,
            object->environmentReflectionSettingData_.textureInfluence,
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
		object->object3DCommon_->SkinningCommonDraw();
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
			common->SkinningCommonDraw();
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
		common->BindSceneLighting(true,true);
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
		float shininess=60;

		bool operator==(const BatchKey& other) const
		{
			return common == other.common &&
				model == other.model &&
				skyboxFilePath == other.skyboxFilePath &&
				enableLighting == other.enableLighting &&
				reflectionStrength == other.reflectionStrength &&
				roughness == other.roughness &&
				textureInfluence == other.textureInfluence && shininess==other.shininess;
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
			value ^= std::hash<float>{}(key.shininess)+0x9e3779b9u+(value<<6)+(value>>2);
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
			object->materialData_.shininess,
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
		common->BindSceneLighting(false,true);
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
	if (modelLease_.get() != model) {
		ModelManager::GetInstance()->PinModel(model);
		modelLease_.reset();
	}
	model_ = model;
	InitializeSkinningState();
}
void Object3D::SetModelLease(std::shared_ptr<Model> model)
{
	modelLease_ = std::move(model); model_ = modelLease_.get();
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
	if (!controllerTransition_) {animationController_.reset();animationControllerRevision_.reset();animationLayers_.reset();}
	animationBlendActive_ = false;
	skinPaletteCacheKeyValid_ = false;
	if (animationClipName_ == clipName) {
		return;
	}
	animationClipName_ = clipName;
	animationTime = 0.0f;
	animationNormalizedTime_=controllerObservedTime_=0;
	animationUpdateFrame_ = 0;
	if (model_ && model_->FindAnimationClip(animationClipName_)) {
		skeleton_ = model_->GetSkeleton();
		SkeletonUpdate(skeleton_);
		SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
	}
}

bool Object3D::BlendToAnimationClip(const std::string& clipName, float durationSeconds)
{
	if (animationController_ && !controllerTransition_) return false;
	if (!model_ || !model_->HasSkinningData() ||
		!std::isfinite(durationSeconds) || durationSeconds < 0.0f || durationSeconds > 60.0f) return false;
	const auto* source = model_->FindAnimationClip(animationClipName_);
	const auto* target = model_->FindAnimationClip(clipName);
	if (!target || target->nodeAnimations.empty() ||
		model_->GetSkeleton().joints.empty()) return false;
	std::vector<QuaternionTransform> admission, targetPose, snapshot;
	if (!Engine::AnimationSystem::SampleLocalPose(model_->GetSkeleton(), *target, 0.0f, targetPose)) return false;
	if (animationBlendActive_) {
		snapshot = animationBlendLastPose_;
		if (snapshot.size() != model_->GetSkeleton().joints.size() ||
			!Engine::AnimationSystem::BlendLocalPoses(snapshot,targetPose,0.0f,admission)) return false;
	} else if (!source || source->nodeAnimations.empty() ||
		!Engine::AnimationSystem::SampleBlendedLocalPose(model_->GetSkeleton(), *source,
			animationTime, *target, 0.0f, 0.0f, admission)) return false;
	if (durationSeconds == 0.0f) { SetAnimationClip(clipName); ResetAnimationTime(); return true; }
	// Complete potentially allocating strings before publishing the new intent.
	std::string outgoing = animationClipName_, incoming = clipName;
	animationBlendSourceClip_.swap(outgoing);
	animationClipName_.swap(incoming);
	animationBlendSnapshot_.swap(snapshot);
	animationBlendLastPose_.swap(admission);
	animationBlendSourceTime_ = animationTime;
	animationBlendSourceSpeed_ = animationSpeed_;
	animationBlendSourceLoop_ = animationLoop_;
	animationTime = 0.0f;
	animationNormalizedTime_=controllerObservedTime_=0;
	animationBlendElapsed_ = 0.0f;
	animationBlendDuration_ = durationSeconds;
	animationBlendActive_ = true;
	skinPaletteCacheKeyValid_ = false;
	return true;
}

bool Object3D::BindAnimationController(std::string_view text)
{
	return BindAnimationController(text,1,true);
}
bool Object3D::BindAnimationController(std::string_view text,float authoredRate,bool playing)
{
	if (!model_ || !model_->HasSkinningData() || model_->GetSkeleton().joints.empty()) return false;
	auto prepared=Engine::AnimationSystem::Controller::Runtime::FromText(text);
	if (!prepared) return false;
	const auto& definition=prepared->Definition();
	if(!std::isfinite(authoredRate) || authoredRate<0)return false;
	for(const auto& layer:definition.layers)for(const auto& state:layer.states)
		if(double(state.speed)*authoredRate>(std::numeric_limits<float>::max)())return false;
	const float effectiveRate=playing?authoredRate:0;
	if(definition.layers.size()!=1 || definition.layers[0].mode!="override" || definition.layers[0].weight!=1 || !definition.layers[0].mask.empty()) {
		auto player=Engine::AnimationSystem::Controller::LayeredPlayer::Prepare(*prepared,model_->GetSkeleton(),[this](const auto& name){return model_->FindAnimationClip(name);});
		if(!player)return false;
		std::unique_ptr<Engine::AnimationSystem::Controller::Runtime> nextRuntime;
		std::unique_ptr<Engine::AnimationSystem::Controller::LayeredPlayer> nextPlayer;
		std::vector<QuaternionTransform> pose;
		if(!player->PrepareFrame(*prepared,0,effectiveRate,[this](const auto& name){return model_->FindAnimationClip(name);},nextRuntime,nextPlayer,pose))return false;
		Skeleton nextSkeleton;std::vector<Matrix4x4> nextMatrices;std::vector<WellForGPU> nextPalette;
		if(!PrepareLayerPalette(model_->GetSkeleton(),model_->GetSkinCluster(),pose,nextSkeleton,nextMatrices,nextPalette))return false;
		const auto& entry=*nextRuntime->ActiveState();std::string entryClip=entry.clip;
		animationClipName_.swap(entryClip);animationController_.swap(nextRuntime);animationLayers_.swap(nextPlayer);
		std::swap(skeleton_,nextSkeleton);skeletonPose_.swap(nextMatrices);skinPaletteData_.swap(nextPalette);
		animationControllerRevision_.reset();animationControllerRate_=effectiveRate;animationSpeed_=entry.speed*effectiveRate;animationLoop_=entry.loop;
		ResetAnimationTime();return true;
	}
	std::vector<QuaternionTransform> admission;
	for (const auto& state:definition.layers[0].states) {
		const auto* clip=model_->FindAnimationClip(state.clip);
		if (!clip || clip->nodeAnimations.empty() || !Engine::AnimationSystem::SampleLocalPose(model_->GetSkeleton(),*clip,0,admission)) return false;
	}
	const auto& entry=*prepared->ActiveState();std::string entryClip=entry.clip;
	const auto* clip=model_->FindAnimationClip(entryClip);
	if (!Engine::AnimationSystem::SampleLocalPose(model_->GetSkeleton(),*clip,0,admission) || admission.size()!=skeleton_.joints.size()) return false;
	animationClipName_.swap(entryClip);animationController_.swap(prepared);animationLayers_.reset();
	animationControllerRate_=effectiveRate;animationSpeed_=entry.speed*effectiveRate;animationLoop_=entry.loop;ResetAnimationTime();
	for(size_t i=0;i<admission.size();++i)skeleton_.joints[i].transform=admission[i];
	SkeletonUpdate(skeleton_);SkinClusterUpdate(model_->GetSkinCluster(),skeleton_);
	animationControllerRevision_.reset();
	return true;
}

bool Object3D::BindAnimationControllerAsset(const Engine::Base::ProjectContext& project,
	const Engine::Assets::AssetRegistry& registry,const std::string& guid,std::string& error,
	const Engine::Assets::AnimatorControllerRevision* expected)
{
	return BindAnimationControllerAssetAtRate(project,registry,guid,1,true,error,expected);
}
bool Object3D::BindAnimationControllerAssetAtRate(const Engine::Base::ProjectContext& project,
	const Engine::Assets::AssetRegistry& registry,const std::string& guid,float authoredRate,bool playing,
	std::string& error,const Engine::Assets::AnimatorControllerRevision* expected)
{
	Engine::Assets::HeldAnimatorControllerAsset lease;
	if (!lease.Open(project,registry,guid,error,expected)) return false;
	// Prepare all revision allocation before live pose/controller publication.
	std::shared_ptr<const Engine::Assets::AnimatorControllerRevision> revision=
		std::make_shared<const Engine::Assets::AnimatorControllerRevision>(*lease.Snapshot());
	if (!BindAnimationController(revision->bytes,authoredRate,playing)) {
		error="Controller graph/clip/mask/palette or authored playback rate is incompatible with this Model";
		return false;
	}
	animationControllerRevision_.swap(revision);error.clear();return true;
}

bool Object3D::SetControllerPlaybackRate(float rate)
{
	if(!animationController_ || !std::isfinite(rate) || rate<0)return false;
	for(const auto& layer:animationController_->Definition().layers)for(const auto& state:layer.states)
		if(double(state.speed)*rate>(std::numeric_limits<float>::max)())return false;
	animationControllerRate_=rate;
	animationSpeed_=animationController_->ActiveState()->speed*rate;return true;
}

void Object3D::SetAnimationSpeed(float speed) {
	if(animationController_ && animationLayers_) {
		const auto stateSpeed=animationController_->ActiveState()->speed;
		const double rate=stateSpeed>0?double(speed)/stateSpeed:speed;
		if(std::isfinite(rate) && rate>=0 && rate<=(std::numeric_limits<float>::max)() && SetControllerPlaybackRate(float(rate)))return;
		animationControllerRate_=0;
	}
	animationSpeed_=speed;
}
void Object3D::ResetAnimationTime() {
	animationTime=0;animationUpdateFrame_=0;skinPaletteCacheKeyValid_=false;animationBlendActive_=false;
	animationNormalizedTime_=controllerObservedTime_=0;if(animationLayers_)animationLayers_->Reset();
}
std::optional<float> Object3D::GetControllerLayerWeight(size_t layer) const {
	if(!animationController_ || !animationController_->ActiveState(layer))return {};
	return animationLayers_?animationLayers_->Weight(layer):std::optional<float>(animationController_->Definition().layers[layer].weight);
}
std::optional<float> Object3D::GetControllerLayerTime(size_t layer) const {
	if(!animationController_ || !animationController_->ActiveState(layer))return {};
	return animationLayers_?std::optional<float>(animationLayers_->GetClock(layer)->time):std::optional<float>(animationTime);
}
std::optional<double> Object3D::GetControllerLayerProgress(size_t layer) const {
	if(!animationController_ || !animationController_->ActiveState(layer))return {};
	return animationLayers_?std::optional<double>(animationLayers_->GetClock(layer)->normalized):std::optional<double>(animationNormalizedTime_);
}
bool Object3D::SetControllerLayerWeight(size_t layer,float weight) {
	if(!animationController_ || !animationController_->ActiveState(layer) || !std::isfinite(weight) || weight<0 || weight>1)return false;
	if(animationLayers_)return animationLayers_->SetWeight(layer,weight);
	if(weight==1)return true;
	try {
		float rate=0;auto player=PrepareControllerLayers(rate);
		if(!player)return false;
		if(!player->SetWeight(layer,weight))return false;
		animationControllerRate_=rate;animationLayers_.swap(player);skinPaletteCacheKeyValid_=false;return true;
	}catch(const std::exception&){return false;}
}
std::unique_ptr<Engine::AnimationSystem::Controller::LayeredPlayer> Object3D::PrepareControllerLayers(float& rate) const {
	using Player=Engine::AnimationSystem::Controller::LayeredPlayer;
	if(!animationController_ || !model_)return {};
	const auto speed=animationController_->ActiveState()->speed;
	const double multiplier=animationLayers_?animationControllerRate_:(speed>0?double(animationSpeed_)/speed:animationControllerRate_);
	if(!std::isfinite(multiplier) || multiplier<0 || multiplier>(std::numeric_limits<float>::max)())return {};
	for(const auto& layer:animationController_->Definition().layers)for(const auto& state:layer.states)
		if(double(state.speed)*multiplier>(std::numeric_limits<float>::max)())return {};
	rate=float(multiplier);
	if(animationLayers_)return std::make_unique<Player>(*animationLayers_);
	auto player=Player::Prepare(*animationController_,model_->GetSkeleton(),[this](const auto& name){return model_->FindAnimationClip(name);});
	if(!player)return {};
	Player::Clock clock;clock.time=animationTime;clock.normalized=animationNormalizedTime_;clock.observed=controllerObservedTime_;
	clock.blending=animationBlendActive_;clock.sourceClip=animationBlendSourceClip_;clock.sourceTime=animationBlendSourceTime_;
	clock.sourceSpeed=animationBlendSourceSpeed_;clock.sourceLoop=animationBlendSourceLoop_;clock.elapsed=animationBlendElapsed_;clock.duration=animationBlendDuration_;
	clock.snapshot=animationBlendSnapshot_;clock.lastPose=animationBlendLastPose_;player->AdoptBaseClock(std::move(clock));return player;
}
std::vector<std::string> Object3D::GetControllerStateNames(size_t layer) const {
	std::vector<std::string> result;
	if(animationController_ && layer<animationController_->Definition().layers.size())
		for(const auto& state:animationController_->Definition().layers[layer].states)result.push_back(state.name);
	return result;
}
bool Object3D::ForceControllerState(size_t layer,std::string_view name,float duration) {
	if(!animationController_ || !animationController_->ActiveState(layer))return false;
	try {
		float rate=0;auto player=PrepareControllerLayers(rate);if(!player)return false;
		std::unique_ptr<Engine::AnimationSystem::Controller::Runtime> nextRuntime;
		std::unique_ptr<Engine::AnimationSystem::Controller::LayeredPlayer> nextPlayer;std::vector<QuaternionTransform> pose;
		if(!player->PrepareForcedFrame(*animationController_,layer,name,duration,rate,[this](const auto& clip){return model_->FindAnimationClip(clip);},nextRuntime,nextPlayer,pose))return false;
		return CommitLayeredFrame(nextRuntime,nextPlayer,pose,rate);
	}catch(const std::exception&){return false;}
}
std::optional<double> Object3D::GetControllerLayerNormalizedTime(size_t layer) const {
	const auto progress=GetControllerLayerProgress(layer);
	if(!progress || !std::isfinite(*progress) || *progress<0 || !model_)return {};
	const auto* state=animationController_->ActiveState(layer);const auto* clip=model_->FindAnimationClip(state->clip);
	if(!clip || !std::isfinite(clip->duration) || clip->duration<0)return {};
	return state->loop && clip->duration>0?std::fmod(*progress,1.0):(std::min)(*progress,1.0);
}
void Object3D::UpdateLayeredController(float delta) {
	if(!enableAnimation_ || !model_)return;
	const auto& bound=animationLayers_->BindSkeleton();const auto& current=model_->GetSkeleton();
	if(bound.joints.size()!=current.joints.size())return;
	for(size_t i=0;i<bound.joints.size();++i)if(bound.joints[i].name!=current.joints[i].name || bound.joints[i].parent!=current.joints[i].parent)return;
	std::unique_ptr<Engine::AnimationSystem::Controller::Runtime> nextRuntime;
	std::unique_ptr<Engine::AnimationSystem::Controller::LayeredPlayer> nextPlayer;std::vector<QuaternionTransform> pose;
	if(!animationLayers_->PrepareFrame(*animationController_,delta,animationControllerRate_,[this](const auto& name){return model_->FindAnimationClip(name);},nextRuntime,nextPlayer,pose))return;
	CommitLayeredFrame(nextRuntime,nextPlayer,pose,animationControllerRate_);
}
bool Object3D::CommitLayeredFrame(std::unique_ptr<Engine::AnimationSystem::Controller::Runtime>& nextRuntime,
	std::unique_ptr<Engine::AnimationSystem::Controller::LayeredPlayer>& nextPlayer,
	const std::vector<QuaternionTransform>& pose,float rate) {
	if(!model_ || !nextRuntime || !nextPlayer)return false;
	const auto& bound=nextPlayer->BindSkeleton();const auto& current=model_->GetSkeleton();
	if(bound.joints.size()!=current.joints.size())return false;
	for(size_t i=0;i<bound.joints.size();++i)if(bound.joints[i].name!=current.joints[i].name || bound.joints[i].parent!=current.joints[i].parent)return false;
	Skeleton nextSkeleton;std::vector<Matrix4x4> nextMatrices;std::vector<WellForGPU> nextPalette;
	if(!PrepareLayerPalette(bound,model_->GetSkinCluster(),pose,nextSkeleton,nextMatrices,nextPalette))return false;
	const auto& state=*nextRuntime->ActiveState();std::string nextClip=state.clip;
	const auto* baseClock=nextPlayer->GetClock(0);
	const float baseTime=baseClock->time;const double baseNormalized=baseClock->normalized,baseObserved=baseClock->observed;
	animationController_.swap(nextRuntime);animationLayers_.swap(nextPlayer);animationClipName_.swap(nextClip);
	std::swap(skeleton_,nextSkeleton);skeletonPose_.swap(nextMatrices);skinPaletteData_.swap(nextPalette);
	animationTime=baseTime;animationNormalizedTime_=baseNormalized;controllerObservedTime_=baseObserved;
	animationControllerRate_=rate;animationSpeed_=state.speed*rate;animationLoop_=state.loop;skinPaletteCacheKeyValid_=false;
	animationBlendActive_=false;return true;
}

void Object3D::SetAnimationUpdateStride(uint32_t stride)
{
	animationUpdateStride_ = (std::max)(1u, stride);
}

bool Object3D::HasAnimationClip(const std::string& clipName) const
{
	return model_ && model_->FindAnimationClip(clipName) != nullptr;
}

std::optional<float> Object3D::GetAnimationClipDuration(const std::string& clipName) const
{
    const auto* clip=model_ && model_->HasSkinningData()?model_->FindAnimationClip(clipName):nullptr;
    if(!clip || clip->nodeAnimations.empty() || !std::isfinite(clip->duration) || clip->duration<0)return {};
    return clip->duration;
}

std::unique_ptr<Object3D> Object3D::CreateAnimationPreview() const
{
    if(!modelLease_ || !model_->HasSkinningData())return {};
    auto preview=std::make_unique<Object3D>();
    preview->Initialize(object3DCommon_);
    preview->SetModelLease(modelLease_);
    preview->SetFrustumCullingEnabled(false);
    preview->SetSkyboxFilePath(skyboxFilePath_);
    return preview;
}

bool Object3D::SampleAnimationPreview(const std::string& clipName,float seconds)
{
    if(animationController_ || !std::isfinite(seconds))return false;
    const auto duration=GetAnimationClipDuration(clipName);
    if(!duration)return false;
    const auto time=std::clamp(seconds,0.f,*duration);
    const auto* clip=model_->FindAnimationClip(clipName);
    std::vector<QuaternionTransform> pose;
    if(!Engine::AnimationSystem::SampleLocalPose(model_->GetSkeleton(),*clip,time,pose))return false;
    Skeleton skeleton;std::vector<Matrix4x4> matrices;std::vector<WellForGPU> palette;
    if(!PrepareLayerPalette(model_->GetSkeleton(),model_->GetSkinCluster(),pose,skeleton,matrices,palette))return false;
    animationClipName_=clipName;
    std::swap(skeleton_,skeleton);
    skeletonPose_.swap(matrices);
    skinPaletteData_.swap(palette);
    animationTime=time;
    animationBlendActive_=false;
    skinPaletteCacheKeyValid_=false;
    return true;
}

std::optional<float> Object3D::GetControllerBlendProgress(size_t layer) const noexcept
{
    if(animationLayers_){
        const auto* clock=animationLayers_->GetClock(layer);
        if(clock && clock->blending && clock->duration>0)return std::clamp(clock->elapsed/clock->duration,0.f,1.f);
    }else if(layer==0 && animationController_ && animationBlendActive_ && animationBlendDuration_>0){
        return std::clamp(animationBlendElapsed_/animationBlendDuration_,0.f,1.f);
    }
    return {};
}

std::vector<std::string> Object3D::GetAnimationClipNames() const
{
	return model_ ? model_->GetAnimationClipNames() : std::vector<std::string>{};
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

bool Object3D::GetLocalModelBounds(Vector3& min, Vector3& max) const
{
	if (!model_ || !model_->HasBounds()) return false;
	min = model_->GetModelData().localAabbMin;
	max = model_->GetModelData().localAabbMax;
	return true;
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
	animationController_.reset();animationControllerRevision_.reset();animationLayers_.reset();animationNormalizedTime_=controllerObservedTime_=0;
	animationBlendActive_ = false;
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

void Object3D::UpdateAnimationState(float deltaTime)
{
	if (!enableAnimation_ || !model_) {
		skinPaletteCacheKeyValid_ = false;
		return;
	}
	if (animationBlendActive_) {
		// Blended palettes have two clocks/weight: never share a single-clip cache key.
		skinPaletteCacheKeyValid_ = false;
		const auto* source = model_->FindAnimationClip(animationBlendSourceClip_);
		const auto* target = model_->FindAnimationClip(animationClipName_);
		const bool frozen = !animationBlendSnapshot_.empty();
		if ((!frozen && !source) || !target) return; // no default-clip fallback during a transition
		const float step = std::isfinite(deltaTime) && deltaTime > 0.0f &&
			std::isfinite(animationSpeed_) && animationSpeed_ > 0.0f ? deltaTime : 0.0f;
		const float elapsed = (std::min)(animationBlendDuration_, animationBlendElapsed_ + step);
		const float sourceTime = frozen ? animationBlendSourceTime_ : Engine::Graphics3D::AdvanceAnimationSeconds(animationBlendSourceTime_, source->duration, step, animationBlendSourceSpeed_, animationBlendSourceLoop_);
		const float targetTime = Engine::Graphics3D::AdvanceAnimationSeconds(animationTime, target->duration, step, animationSpeed_, animationLoop_);
		std::vector<QuaternionTransform> pose;
		if (frozen) {
			std::vector<QuaternionTransform> targetPose;
			if (!Engine::AnimationSystem::SampleLocalPose(model_->GetSkeleton(),*target,targetTime,targetPose) ||
				!Engine::AnimationSystem::BlendLocalPoses(animationBlendSnapshot_,targetPose,elapsed / animationBlendDuration_,pose)) return;
		} else if (!Engine::AnimationSystem::SampleBlendedLocalPose(model_->GetSkeleton(), *source,
			sourceTime, *target, targetTime, elapsed / animationBlendDuration_, pose)) return;
		if (pose.size() != skeleton_.joints.size()) return;
		for (size_t i = 0; i < pose.size(); ++i) skeleton_.joints[i].transform = pose[i];
		SkeletonUpdate(skeleton_);
		SkinClusterUpdate(model_->GetSkinCluster(), skeleton_);
		animationBlendLastPose_.swap(pose);
		animationTime = targetTime;
		if (animationController_) animationNormalizedTime_=Engine::Graphics3D::AdvanceControllerNormalizedTime(
			animationNormalizedTime_,targetTime,target->duration,step,animationSpeed_,animationLoop_);
		animationBlendSourceTime_ = sourceTime;
		animationBlendElapsed_ = elapsed;
		if (elapsed >= animationBlendDuration_) animationBlendActive_ = false;
		return;
	}
	const Animation* animation = model_->FindAnimationClip(animationClipName_);
	std::string resolvedClipName = animationClipName_;
	if (!animation) {
		if (animationController_) { skinPaletteCacheKeyValid_=false;return; }
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
			animationLoop_, deltaTime, animationController_ ? &animationNormalizedTime_ : nullptr);
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
			animationLoop_, deltaTime, animationController_ ? &animationNormalizedTime_ : nullptr);
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
		animationLoop_, deltaTime, animationController_ ? &animationNormalizedTime_ : nullptr);
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
	worldMatrix = hasAuthoredWorldMatrix_ ? authoredWorldMatrix_ : MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
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
	cameraForGpu_.worldPosition = activeCamera->GetWorldPosition();
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

