#include "ParticleManager.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "HResult.h"
#include "SrvManager.h"
#include <ModelManager.h>
#include <TextureManager.h>
#include "CameraManager.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <imgui.h>

namespace {
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kQuadHalfSize = 0.5f;
const Vector3 kDefaultParticleNormal = { 0.0f, 0.0f, 1.0f };
const Vector4 kInitialParticleColor = { 1.0f, 1.0f, 1.0f, 0.0f };

bool IsFiniteVector(const Vector3& value)
{
	return std::isfinite(value.x) &&
		std::isfinite(value.y) &&
		std::isfinite(value.z);
}
}

namespace Engine::Particle {

ParticleManager::~ParticleManager() = default;

//シングルトンインスタンスの取得
ParticleManager* ParticleManager::GetInstance()
{
	static ParticleManager instance;
	return &instance;
}

std::shared_ptr<Engine::Base::DirectXCommon> ParticleManager::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

void ParticleManager::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager)
{

	//引数で受け取ったポインタをメンバ変数に代入
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

	// エミット時のばらつきに使う乱数エンジンは初期化時に 1 度だけ用意する
	std::random_device seedGenerator;
	std::mt19937 random(seedGenerator());
	randomEngine = random;

	// 全グループ共通で使うパーティクル用パイプラインを生成する
	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon);
	graphicsPipeline_->CreateParticle();
}

void ParticleManager::Finalize()
{
	// Framework drains/discards commands before retiring shared services.
	graphicsPipeline_.reset();
	if (const auto dx = dxCommon_.lock()) {
		for (const auto& [name, group] : particleGroups) {
			static_cast<void>(name); dx->UntrackResourceState(group.vertexResource.Get());
		}
		for (const auto& retired : retiredVertices_) dx->UntrackResourceState(retired.resource.Get());
	}
	particleGroups.clear(); particleGroupsByIndex_.clear(); retiredVertices_.clear();
	model_ = nullptr; dxCommon_.reset(); srvManager_ = nullptr;
}

void ParticleManager::RetireVertex(ParticleGroup& group) noexcept
{
	if (!group.vertexResource) return;
	if (const auto dx = dxCommon_.lock()) {
		assert(retiredVertices_.size() < retiredVertices_.capacity());
		retiredVertices_.push_back({dx->GetPendingSubmissionFenceValue(), std::move(group.vertexResource)});
	} else group.vertexResource.Reset();
}

void ParticleManager::CollectRetiredGroups() noexcept
{
	const auto dx = dxCommon_.lock(); if (!dx) return;
	const auto completed = dx->GetCompletedFenceValue();
	std::erase_if(retiredVertices_, [&](const RetiredVertex& retired) {
		if (retired.fence > completed) return false;
		dx->UntrackResourceState(retired.resource.Get()); return true;
	});
}

void ParticleManager::ReleaseOwnedGroup(ParticleGroupHandle handle) noexcept
{
	auto* group = ResolveParticleGroup(handle); if (!group || !group->owned) return;
	const auto found = particleGroups.find(group->key);
	RetireVertex(*group); particleGroupsByIndex_[handle.index] = nullptr;
	particleGroups.erase(found);
}

ParticleGroupOwner::~ParticleGroupOwner() { Reset(); }
ParticleGroupOwner::ParticleGroupOwner(ParticleGroupOwner&& other) noexcept
	: manager_(std::exchange(other.manager_, nullptr)), handle_(std::exchange(other.handle_, {})) {}
ParticleGroupOwner& ParticleGroupOwner::operator=(ParticleGroupOwner&& other) noexcept
{
	if (this != &other) { Reset(); manager_ = std::exchange(other.manager_, nullptr); handle_ = std::exchange(other.handle_, {}); }
	return *this;
}
ParticleGroupOwner::operator bool() const noexcept { return manager_ && manager_->ResolveParticleGroup(handle_); }
void ParticleGroupOwner::Reset() noexcept
{
	if (manager_) manager_->ReleaseOwnedGroup(handle_);
	manager_ = nullptr; handle_ = {};
}
void ParticleGroupOwner::Emit(const Vector3& position, uint32_t count)
{
	if (manager_) manager_->Emit(handle_, position, count);
}
bool ParticleGroupOwner::Update(float deltaTime)
{
	if (!manager_ || !std::isfinite(deltaTime) || deltaTime < 0) return false;
	auto* group = manager_->ResolveParticleGroup(handle_); if (!group) return false;
	manager_->CollectRetiredGroups(); group->droppedLastFrame = group->droppedThisFrame;
	group->droppedThisFrame = 0; group->emittedThisFrame = 0;
	const Matrix4x4 identity = Matrix4x4{}.MakeIdentity4x4();
	manager_->UpdateParticleGroup(*group, manager_->ResolveDeltaTime(deltaTime), identity, identity);
	return true;
}
bool ParticleGroupOwner::Draw(const Matrix4x4& view, const Matrix4x4& projection)
{
	if (!manager_) return false;
	auto* group = manager_->ResolveParticleGroup(handle_); if (!group) return false;
	manager_->lastDrawCallCount_ = manager_->lastDrawnInstanceCount_ = 0;
	manager_->DrawParticleGroup(*group, &view, &projection); return true;
}
bool ParticleGroupOwner::Draw(const Matrix4x4& view,const Matrix4x4& projection,const Matrix4x4& model)
{
    if(!manager_)return false;auto* group=manager_->ResolveParticleGroup(handle_);if(!group)return false;
    manager_->lastDrawCallCount_=manager_->lastDrawnInstanceCount_=0;manager_->DrawParticleGroup(*group,&view,&projection,&model);return true;
}
void ParticleGroupOwner::SetLifetimeFade(bool enabled) noexcept
{
    if(manager_)if(auto* group=manager_->ResolveParticleGroup(handle_))group->lifetimeFade=enabled;
}

void ParticleManager::Update()
{
	Update(kFixedParticleDeltaTime);
}

void ParticleManager::Update(float deltaTime)
{
	CollectRetiredGroups();
	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		return;
	}
	const float appliedDeltaTime = ResolveDeltaTime(deltaTime);
	lastAppliedDeltaTime_ = appliedDeltaTime;
	globalDroppedLastFrame_ = globalDroppedThisFrame_;
	globalDroppedThisFrame_ = 0;

	Matrix4x4 viewMatrix = activeCamera->GetViewMatrix();
	Matrix4x4 projectionMatrix = activeCamera->GetProjectionMatrix();

	for (auto& particleGroupEntry : particleGroups) {
		ParticleGroup& particleGroup = particleGroupEntry.second;
		if (particleGroup.owned) continue;
		particleGroup.droppedLastFrame = particleGroup.droppedThisFrame;
		particleGroup.droppedThisFrame = 0;
		particleGroup.emittedThisFrame = 0;
		UpdateParticleGroup(particleGroup, appliedDeltaTime, viewMatrix, projectionMatrix);
	}
}

float ParticleManager::ResolveDeltaTime(float deltaTime) const
{
	if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) { return 0.0f; }
	if (useFixedDeltaTime_) {
		return kFixedParticleDeltaTime;
	}
	return std::clamp(deltaTime, 0.0f, kMaxParticleDeltaTime);
}

void ParticleManager::ClearParticles(const std::string& groupName)
{
    auto found = particleGroups.find(groupName);
    if (found == particleGroups.end()) { return; }
    auto& group = found->second;
    group.particles.clear(); group.instanceCount = 0;
    group.emittedThisFrame = 0;
}

void ParticleManager::UpdateParticleGroup(ParticleGroup& particleGroup, float deltaTime, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
	auto& behavior = particleGroup.behavior;
	if (!behavior) {
		particleGroup.instanceCount = 0;
		return;
	}
	uint32_t counter = 0;
	size_t writeIndex = 0;
	for (size_t readIndex = 0; readIndex < particleGroup.particles.size(); ++readIndex) {
		Particle& particle = particleGroup.particles[readIndex];
		if (particle.lifetime <= particle.currentTime) {
			continue;
		}

		behavior->Update(particle, deltaTime, &particleGroup.materialData);
		UpdateAliveParticle(particle, particleGroup, counter, viewMatrix, projectionMatrix);
		if (writeIndex != readIndex) {
			particleGroup.particles[writeIndex] = particle;
		}
		++writeIndex;
	}
	particleGroup.particles.resize(writeIndex);

	// このフレームに実際に生存していた数だけ描画数へ反映する
	particleGroup.instanceCount = counter;
}

void ParticleManager::UpdateAliveParticle(Particle& particle, ParticleGroup& particleGroup, uint32_t& counter, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
	const float lifetimeAlpha = std::clamp(
		1.0f - (particle.currentTime / particle.lifetime),
		0.0f,
		1.0f);
	Matrix4x4 rotateMatrix = MyMath::MakeRotateMatrix(particle.transform.rotate);
	Matrix4x4 worldMatrix = MyMath::MakeScaleMatrix(particle.transform.scale) * rotateMatrix * MyMath::MakeTranslateMatrix(particle.transform.translate);
	Matrix4x4 worldViewProjectionMatrix = worldMatrix * viewMatrix * projectionMatrix;

	// 生存中インスタンスだけを前から詰めて書き込み、残りは描画対象から外す
	if (counter < particleGroup.maxInstanceCount) {
		particleGroup.instanceData[counter].WVP = worldViewProjectionMatrix;
		particleGroup.instanceData[counter].World = worldMatrix;
		particleGroup.instanceData[counter].color = particle.color;
		if(particleGroup.lifetimeFade)particleGroup.instanceData[counter].color.w *= lifetimeAlpha;
		++counter;
	}
}

void ParticleManager::Draw()
{
	lastDrawCallCount_ = lastDrawnInstanceCount_ = 0;
	for (auto& [name, group] : particleGroups) {
		static_cast<void>(name); if (!group.owned) DrawParticleGroup(group);
	}
}

void ParticleManager::DrawParticleGroup(ParticleGroup& group, const Matrix4x4* view, const Matrix4x4* projection, const Matrix4x4* model)
{
	if (!group.instanceCount) return;
	const auto finite = [](const Matrix4x4& matrix) {
		for (const auto& row : matrix.m) for (const auto value : row) if (!std::isfinite(value)) return false;
		return true;
	};
	if (view && (!projection || !finite(*view) || !finite(*projection)))
		throw std::invalid_argument("Invalid Particle view/projection");
    if(model&&(!view||!finite(*model)||model->m[0][3]!=0||model->m[1][3]!=0||model->m[2][3]!=0||model->m[3][3]!=1))throw std::invalid_argument("Invalid Particle model matrix");
	const auto dx = GetDirectXCommon();
	const auto material = dx->AllocateFrameUpload(sizeof(Material), 256);
	std::memcpy(material.cpuAddress, &group.materialData, sizeof(Material));
	const auto instances = dx->AllocateFrameUpload(sizeof(ParticleForGPU) * group.instanceCount, sizeof(ParticleForGPU));
	auto* output = static_cast<ParticleForGPU*>(instances.cpuAddress);
	for (uint32_t i = 0; i < group.instanceCount; ++i) {
		output[i] = group.instanceData[i];
		if (view) {
			if(model)output[i].World=output[i].World * *model;
			output[i].WVP = output[i].World * *view * *projection;
			if (!finite(output[i].WVP)) throw std::invalid_argument("Particle view transform overflow");
		}
	}
	// A descriptor is immutable until its pending submission fence completes.
	// Each draw/view gets a fresh allocation; upload memory is frame-owned.
	struct Descriptor {
		Base::SrvManager* manager; uint32_t index;
		~Descriptor() { manager->Free(index); }
	} descriptor{srvManager_, srvManager_->Allocate()};
	srvManager_->LabelUsage(descriptor.index, "ParticleViewInstances");
	srvManager_->CreateSRVforStructuredBuffer(descriptor.index, instances.resource,
		group.instanceCount, sizeof(ParticleForGPU), instances.offset / sizeof(ParticleForGPU));
	auto* commands = dx->GetCommandList();
	commands->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureParticleHandle().Get());
	commands->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateParticleHandle().Get());
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commands->IASetVertexBuffers(0, 1, &group.vertexBufferView);
	commands->SetGraphicsRootConstantBufferView(0, material.gpuAddress);
	srvManager_->SetGraphicsRootDescriptorTable(2, group.materialdata.textureIndex);
	srvManager_->SetGraphicsRootDescriptorTable(1, descriptor.index);
	commands->DrawInstanced(group.vertexCount, group.instanceCount, 0, 0);
	++lastDrawCallCount_; lastDrawnInstanceCount_ += group.instanceCount;
}

ParticleGroupHandle ParticleManager::CreateParticleGroup(const std::string& name,
	const std::string& texture, VerticesType type, std::unique_ptr<IParticleBehavior> behavior, uint32_t capacity)
{
	if (const auto found = particleGroups.find(name); found != particleGroups.end()) {
		if (found->second.owned) throw std::invalid_argument("Particle name belongs to an owned group");
		return found->second.handle;
	}
	return CreateGroup(name, texture, type, std::move(behavior), (std::max)(1u, capacity), false);
}

ParticleGroupOwner ParticleManager::CreateOwnedParticleGroup(const std::string& name,
	const std::string& texture, VerticesType type, std::unique_ptr<IParticleBehavior> behavior, uint32_t capacity)
{
	if (name.empty() || name.size() > 256 || name.find('\0') != std::string::npos || !capacity || capacity > 65536)
		throw std::invalid_argument("Invalid owned Particle name/capacity");
	return ParticleGroupOwner(this, CreateGroup(name, texture, type, std::move(behavior), capacity, true));
}

ParticleGroupHandle ParticleManager::CreateGroup(const std::string& name, const std::string& texture,
	VerticesType type, std::unique_ptr<IParticleBehavior> behavior, uint32_t capacity, bool owned)
{
	static_assert(std::is_nothrow_move_assignable_v<ParticleGroup>);
	if (!srvManager_ || !graphicsPipeline_ || dxCommon_.expired()) throw std::logic_error("Particle manager is not initialized");
	if (particleGroups.contains(name)) throw std::invalid_argument("Particle group name already exists");
	if (type != VerticesType::Quad && type != VerticesType::Ring && type != VerticesType::Cylinder)
		throw std::invalid_argument("Invalid Particle vertices type");
	if (particleGroupGeneration_ > UINT32_MAX) throw std::overflow_error("Particle handle generations exhausted");
	CollectRetiredGroups();
	// Also reserves the failure candidate's retirement before any GPU resource.
	retiredVertices_.reserve(retiredVertices_.size() + particleGroups.size() + 1);
	const auto free = std::find(particleGroupsByIndex_.begin(), particleGroupsByIndex_.end(), nullptr);
	const auto slot = static_cast<size_t>(free - particleGroupsByIndex_.begin());
	if (slot >= UINT32_MAX) throw std::overflow_error("Particle handle slots exhausted");
	particleGroupsByIndex_.reserve(particleGroupsByIndex_.size() + 1);
	ParticleGroup candidate{};
	try {
		candidate.key = name; candidate.debugName = name; candidate.owned = owned;
		candidate.handle = {static_cast<uint32_t>(slot), static_cast<uint32_t>(particleGroupGeneration_)};
		candidate.maxInstanceCount = capacity; candidate.behavior = std::move(behavior);
		InitializeParticleGroupMaterial(candidate); InitializeParticleGroupVertices(candidate, type);
		InitializeParticleGroupTexture(candidate, texture); InitializeParticleGroupInstances(candidate);
		auto [entry, inserted] = particleGroups.try_emplace(name);
		if (!inserted) throw std::logic_error("Particle group changed during creation");
		entry->second = std::move(candidate);
		if (slot == particleGroupsByIndex_.size()) particleGroupsByIndex_.push_back(&entry->second);
		else particleGroupsByIndex_[slot] = &entry->second;
		++particleGroupGeneration_;
		return entry->second.handle;
	} catch (...) { RetireVertex(candidate); throw; }
}

void ParticleManager::InitializeParticleGroupMaterial(ParticleGroup& particleGroup)
{
	particleGroup.materialData.color = { Vector4(1.0f, 1.0f, 1.0f, 1.0f) };
	particleGroup.materialData.enableLighting = false;
	particleGroup.materialData.uvTransform =
		particleGroup.materialData.uvTransform.MakeIdentity4x4();
}

void ParticleManager::InitializeParticleGroupVertices(ParticleGroup& particleGroup, VerticesType verticesType)
{
	const auto dxCommon = GetDirectXCommon();
	std::vector<VertexData> vertices = MakeCylinderVertices();
	switch (verticesType) {
	case VerticesType::Quad:
		vertices = MakeQuadVertices();
		break;
	case VerticesType::Ring:
		vertices = MakeRingVertices();
		break;
	case VerticesType::Cylinder:
		vertices = MakeCylinderVertices();
		break;
	}

	particleGroup.vertexCount = static_cast<uint32_t>(vertices.size());
	const size_t vertexBytes = sizeof(VertexData) * vertices.size();
	particleGroup.vertexResource = dxCommon->CreateDefaultBufferResource(
		vertices.data(),
		vertexBytes,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	particleGroup.vertexBufferView.BufferLocation = particleGroup.vertexResource->GetGPUVirtualAddress();
	particleGroup.vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
	particleGroup.vertexBufferView.StrideInBytes = sizeof(VertexData);
}

void ParticleManager::InitializeParticleGroupTexture(ParticleGroup& particleGroup, const std::string& textureFilePath)
{
	particleGroup.materialdata.textureFilePath = textureFilePath;
	Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath);
	particleGroup.materialdata.textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);
}

void ParticleManager::InitializeParticleGroupInstances(ParticleGroup& particleGroup)
{
	particleGroup.instanceCount = 0;
	particleGroup.instanceData.resize(particleGroup.maxInstanceCount);

	ParticleForGPU particleForGPU;
	particleForGPU.WVP = particleForGPU.WVP.MakeIdentity4x4();
	particleForGPU.World = particleForGPU.World.MakeIdentity4x4();
	particleForGPU.color = kInitialParticleColor;
	for (uint32_t index = 0; index < particleGroup.maxInstanceCount; ++index) {
		particleGroup.instanceData[index] = particleForGPU;
	}


}

void ParticleManager::Emit(const std::string& name, const Vector3& position, uint32_t count)
{
	const std::optional<ParticleGroupHandle> handle =
		GetParticleGroupHandle(name);
	if (!handle) {
		return;
	}
	Emit(*handle, position, count);
}

void ParticleManager::Emit(
	ParticleGroupHandle handle,
	const Vector3& position,
	uint32_t count)
{
	ParticleGroup* resolvedGroup = ResolveParticleGroup(handle);
	if (!resolvedGroup) {
		return;
	}
	ParticleGroup& particleGroup = *resolvedGroup;
	if (!particleGroup.behavior) {
		return;
	}

	const uint32_t frameBudget = ResolveEffectiveEmissionLimit(particleGroup);
	const uint32_t remainingFrameBudget =
		particleGroup.emittedThisFrame < frameBudget
			? frameBudget - particleGroup.emittedThisFrame
			: 0u;
	if (remainingFrameBudget == 0u) {
		particleGroup.droppedThisFrame += count;
		return;
	}
	const size_t activeCount = particleGroup.particles.size();
	const size_t totalActiveCount = GetTotalActiveParticleCount();
	const uint32_t globalAvailableCount =
		totalActiveCount < maxTotalActiveParticleCount_
		? static_cast<uint32_t>(maxTotalActiveParticleCount_ - totalActiveCount)
		: 0u;
	const uint32_t availableCount = activeCount < particleGroup.maxInstanceCount
		? particleGroup.maxInstanceCount - static_cast<uint32_t>(activeCount)
		: 0u;
	const uint32_t emitCount = (std::min)(
		(std::min)(
			(std::min)(count, availableCount),
			remainingFrameBudget),
		globalAvailableCount);
	particleGroup.droppedThisFrame += count - emitCount;
	globalDroppedThisFrame_ += count - emitCount;
	if (emitCount == 0) {
		return;
	}

	particleGroup.particles.reserve(activeCount + emitCount);
	for (uint32_t i = 0; i < emitCount; ++i) {
		particleGroup.particles.push_back(particleGroup.behavior->Create(randomEngine, position));
	}
	particleGroup.emittedThisFrame += emitCount;
}

void ParticleManager::EmitTrailSegment(
	const std::string& name,
	const Vector3& start,
	const Vector3& end,
	float width)
{
	const std::optional<ParticleGroupHandle> handle =
		GetParticleGroupHandle(name);
	if (!handle) {
		return;
	}
	EmitTrailSegment(*handle, start, end, width);
}

void ParticleManager::EmitTrailSegment(
	ParticleGroupHandle handle,
	const Vector3& start,
	const Vector3& end,
	float width)
{
	ParticleGroup* resolvedGroup = ResolveParticleGroup(handle);
	if (!resolvedGroup) {
		return;
	}
	ParticleGroup& particleGroup = *resolvedGroup;
	if (!particleGroup.behavior || particleGroup.particles.size() >= particleGroup.maxInstanceCount) {
		if (particleGroup.particles.size() >= particleGroup.maxInstanceCount) {
			++particleGroup.droppedThisFrame;
			++globalDroppedThisFrame_;
		}
		return;
	}
	if (GetTotalActiveParticleCount() >= maxTotalActiveParticleCount_) {
		++particleGroup.droppedThisFrame;
		++globalDroppedThisFrame_;
		return;
	}
	if (particleGroup.emittedThisFrame >= ResolveEffectiveEmissionLimit(particleGroup)) {
		++particleGroup.droppedThisFrame;
		++globalDroppedThisFrame_;
		return;
	}

	const Vector3 delta{ end.x - start.x, end.y - start.y, end.z - start.z };
	const float horizontalLength = std::sqrt(delta.x * delta.x + delta.z * delta.z);
	const float length = std::sqrt(horizontalLength * horizontalLength + delta.y * delta.y);
	if (length <= 0.001f) {
		return;
	}

	Particle particle = particleGroup.behavior->Create(
		randomEngine,
		{
			(start.x + end.x) * 0.5f,
			(start.y + end.y) * 0.5f,
			(start.z + end.z) * 0.5f,
		});
	particle.transform.scale = { width, length, 1.0f };
	particle.transform.rotate = {
		std::numbers::pi_v<float> * 0.5f - std::atan2(delta.y, horizontalLength),
		std::atan2(delta.x, delta.z),
		0.0f,
	};
	particleGroup.particles.push_back(particle);
	++particleGroup.emittedThisFrame;
}

bool ParticleManager::EmitTrailSegmentClamped(
	const std::string& name,
	const Vector3& previous,
	const Vector3& current,
	float width,
	float lengthMultiplier,
	float maxFrameDistance)
{
	const std::optional<ParticleGroupHandle> handle =
		GetParticleGroupHandle(name);
	if (!handle) {
		return false;
	}
	return EmitTrailSegmentClamped(
		*handle,
		previous,
		current,
		width,
		lengthMultiplier,
		maxFrameDistance);
}

bool ParticleManager::EmitTrailSegmentClamped(
	ParticleGroupHandle handle,
	const Vector3& previous,
	const Vector3& current,
	float width,
	float lengthMultiplier,
	float maxFrameDistance)
{
	if (!IsFiniteVector(previous) || !IsFiniteVector(current)) {
		return false;
	}

	const Vector3 delta{
		current.x - previous.x,
		current.y - previous.y,
		current.z - previous.z,
	};
	const float deltaLength = std::sqrt(
		delta.x * delta.x +
		delta.y * delta.y +
		delta.z * delta.z);
	if (deltaLength <= 0.001f) {
		return false;
	}

	const float clampedMaxDistance = std::clamp(maxFrameDistance, 0.2f, 14.0f);
	const float clampedScale = deltaLength > clampedMaxDistance
		? clampedMaxDistance / deltaLength
		: 1.0f;
	const Vector3 clampedDelta{
		delta.x * clampedScale,
		delta.y * clampedScale,
		delta.z * clampedScale,
	};
	const float clampedLengthMultiplier =
		std::clamp(lengthMultiplier, 0.0f, 8.0f);
	const Vector3 trailStart{
		current.x - clampedDelta.x * clampedLengthMultiplier,
		current.y - clampedDelta.y * clampedLengthMultiplier,
		current.z - clampedDelta.z * clampedLengthMultiplier,
	};
	EmitTrailSegment(handle, trailStart, current, width);
	return true;
}

void ParticleManager::EmitTrailCircle(
	const std::string& name,
	const Vector3& center,
	float radius,
	float yOffset,
	float width,
	int32_t segments)
{
	const std::optional<ParticleGroupHandle> handle =
		GetParticleGroupHandle(name);
	if (!handle) {
		return;
	}
	EmitTrailCircle(*handle, center, radius, yOffset, width, segments);
}

void ParticleManager::EmitTrailCircle(
	ParticleGroupHandle handle,
	const Vector3& center,
	float radius,
	float yOffset,
	float width,
	int32_t segments)
{
	if (!IsFiniteVector(center) || radius <= 0.0f || segments <= 0) {
		return;
	}

	const float y = center.y + yOffset;
	for (int32_t segmentIndex = 0; segmentIndex < segments; ++segmentIndex) {
		const float startAngle =
			static_cast<float>(segmentIndex) /
			static_cast<float>(segments) * kTwoPi;
		const float endAngle =
			static_cast<float>(segmentIndex + 1) /
			static_cast<float>(segments) * kTwoPi;
		EmitTrailSegment(
			handle,
			{
				center.x + std::cos(startAngle) * radius,
				y,
				center.z + std::sin(startAngle) * radius,
			},
			{
				center.x + std::cos(endAngle) * radius,
				y,
				center.z + std::sin(endAngle) * radius,
			},
			width);
	}
}

void ParticleManager::SetModel(const std::string& filepath)
{
	// モデルを検索してセットする
	model_ = Engine::Graphics3D::ModelManager::GetInstance()->FindModel(filepath);
}

std::vector<VertexData> ParticleManager::MakeRingVertices(uint32_t RingDivide, float outerRadius, float innerRadius)
{
	std::vector<VertexData> ringVertices;
	const float radianPerDivide = kTwoPi / static_cast<float>(RingDivide);

	// リングは内外 2 本の円を 2 三角形ずつでつないで帯状メッシュにする
	for (uint32_t index = 0; index < RingDivide; ++index) {
		float angle = index * radianPerDivide;
		float nextAngle = ((index + 1) % RingDivide) * radianPerDivide;

		float sin = std::sinf(angle);
		float cos = std::cosf(angle);
		float sinnext = std::sinf(nextAngle);
		float cosnext = std::cosf(nextAngle);

		float u = (static_cast<float>(index) / RingDivide);
		float unext = (static_cast<float>(index + 1) / RingDivide);

		VertexData v[] = {
			{ {-sin * outerRadius,  cos * outerRadius,  0.0f, 1.0f},     {u,     0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sin * innerRadius,  cos * innerRadius,  0.0f, 1.0f},     {u,     1.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * outerRadius, cosnext * outerRadius, 0.0f, 1.0f}, {unext, 0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * outerRadius, cosnext * outerRadius, 0.0f, 1.0f}, {unext, 0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sin * innerRadius,  cos * innerRadius,  0.0f, 1.0f},     {u,     1.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * innerRadius, cosnext * innerRadius, 0.0f, 1.0f}, {unext, 1.0f}, {0.0f, 0.0f, 1.0f} }
		};

		for (const auto& vert : v) {
			ringVertices.push_back(vert);
		}
	}

	return ringVertices;
}

std::vector<VertexData> ParticleManager::MakeCylinderVertices(uint32_t cylinderDivide, float topRadius, float bottomRadius, float height)
{
	const float radianPerDivide = kTwoPi / static_cast<float>(cylinderDivide);
	std::vector<VertexData> cylinderVertices;

	// 円周方向の各分割を 2 三角形に分けて側面だけを構成する
	for (uint32_t index = 0; index < cylinderDivide; ++index) {
		float sin = std::sinf(index * radianPerDivide);
		float cos = std::cosf(index * radianPerDivide);
		float sinnext = std::sinf((index + 1) * radianPerDivide);
		float cosnext = std::cosf((index + 1) * radianPerDivide);
		float u = float(index) / float(cylinderDivide);
		float unext = float(index + 1) / float(cylinderDivide);

		VertexData v[] = {
			{{-sin * topRadius, height, cos * topRadius, 1.0f},              {u, 0.0f},     {-sin, 0.0f, cos}},
			{{-sinnext * topRadius, height, cosnext * topRadius, 1.0f},      {unext, 0.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sin * bottomRadius, 0.0f, cos * bottomRadius, 1.0f},          {u, 1.0f},     {-sin, 0.0f, cos}},
			{{-sinnext * topRadius, height, cosnext * topRadius, 1.0f},      {unext, 0.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sinnext * bottomRadius, 0.0f, cosnext * bottomRadius, 1.0f},  {unext, 1.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sin * bottomRadius, 0.0f, cos * bottomRadius, 1.0f},          {u, 1.0f},     {-sin, 0.0f, cos}}
		};

		for (const auto& vert : v) {
			cylinderVertices.push_back(vert);
		}
	}

	return cylinderVertices;
}

std::vector<VertexData> ParticleManager::MakeQuadVertices()
{
	std::vector<VertexData> vertices = {
		{{-kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 1.0f}, kDefaultParticleNormal},
		{{-kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 0.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 1.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 1.0f}, kDefaultParticleNormal},
		{{-kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 0.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 0.0f}, kDefaultParticleNormal},
	};
	return vertices;
}

void ParticleManager::SetBehavior(const std::string& groupName, std::unique_ptr<IParticleBehavior> behavior)
{
	const std::optional<ParticleGroupHandle> handle =
		GetParticleGroupHandle(groupName);
	if (!handle) {
		return;
	}
	SetBehavior(*handle, std::move(behavior));
}

void ParticleManager::SetBehavior(
	ParticleGroupHandle handle,
	std::unique_ptr<IParticleBehavior> behavior)
{
	if (ParticleGroup* particleGroup = ResolveParticleGroup(handle)) {
		particleGroup->behavior = std::move(behavior);
	}
}

std::optional<ParticleGroupHandle>
ParticleManager::GetParticleGroupHandle(
	const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.handle;
}

ParticleManager::ParticleGroup* ParticleManager::ResolveParticleGroup(
	ParticleGroupHandle handle)
{
	if (handle.index >= particleGroupsByIndex_.size() || !particleGroupsByIndex_[handle.index] ||
		particleGroupsByIndex_[handle.index]->handle.generation != handle.generation) {
		return nullptr;
	}
	return particleGroupsByIndex_[handle.index];
}

const ParticleManager::ParticleGroup* ParticleManager::ResolveParticleGroup(
	ParticleGroupHandle handle) const
{
	if (handle.index >= particleGroupsByIndex_.size() || !particleGroupsByIndex_[handle.index] ||
		particleGroupsByIndex_[handle.index]->handle.generation != handle.generation) {
		return nullptr;
	}
	return particleGroupsByIndex_[handle.index];
}

size_t ParticleManager::GetTotalActiveParticleCount() const
{
	size_t total = 0;
	for (const auto& particleGroupEntry : particleGroups) {
		total += particleGroupEntry.second.particles.size();
	}
	return total;
}

std::optional<size_t> ParticleManager::GetActiveParticleCount(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.particles.size();
}

std::optional<uint32_t> ParticleManager::GetParticleGroupMaxInstanceCount(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.maxInstanceCount;
}

void ParticleManager::SetParticleGroupEmissionLimit(
	ParticleGroupHandle handle,
	uint32_t maxEmitsPerFrame)
{
	if (ParticleGroup* particleGroup = ResolveParticleGroup(handle)) {
		particleGroup->maxEmitsPerFrame =
			maxEmitsPerFrame == 0u ? UINT32_MAX : maxEmitsPerFrame;
	}
}

void ParticleManager::SetParticleGroupEmissionLimit(
	const std::string& groupName,
	uint32_t maxEmitsPerFrame)
{
	const auto handle = GetParticleGroupHandle(groupName);
	if (!handle) {
		return;
	}
	SetParticleGroupEmissionLimit(*handle, maxEmitsPerFrame);
}

std::optional<uint32_t> ParticleManager::GetParticleGroupEmissionLimit(
	const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.maxEmitsPerFrame;
}

std::optional<uint32_t> ParticleManager::GetParticleGroupEffectiveEmissionLimit(
	const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return ResolveEffectiveEmissionLimit(it->second);
}

std::optional<uint32_t> ParticleManager::GetParticleGroupDroppedLastFrame(
	const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.droppedLastFrame;
}

void ParticleManager::SetGlobalEmissionScale(float emissionScale)
{
	globalEmissionScale_ = std::clamp(emissionScale, 0.0f, 1.0f);
}

void ParticleManager::SetMaxTotalActiveParticleCount(size_t maxCount)
{
	maxTotalActiveParticleCount_ = (std::max)(size_t{ 1 }, maxCount);
}

uint32_t ParticleManager::ResolveEffectiveEmissionLimit(
	const ParticleGroup& particleGroup) const
{
	if (particleGroup.maxEmitsPerFrame == UINT32_MAX) {
		return UINT32_MAX;
	}
	if (globalEmissionScale_ <= 0.0f) {
		return 0;
	}
	return (std::max)(
		1u,
		static_cast<uint32_t>(
			std::ceil(static_cast<float>(particleGroup.maxEmitsPerFrame) * globalEmissionScale_)));
}

void ParticleManager::SetParticleGroupDebugName(const std::string& groupName, const std::string& debugName)
{
	auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return;
	}
	it->second.debugName = debugName;
}

std::optional<std::string> ParticleManager::GetParticleGroupDebugName(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.debugName;
}

}
